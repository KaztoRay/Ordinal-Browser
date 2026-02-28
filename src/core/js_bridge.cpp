/**
 * @file js_bridge.cpp
 * @brief JavaScript ↔ C++ 브릿지 구현
 * 
 * 네이티브 함수 등록/호출, 이벤트 시스템, 권한 관리를 구현합니다.
 */

#include "js_bridge.h"
#include "v8_engine.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>

namespace ordinal::core {

// ============================================================
// 내부 구현 (PIMPL)
// ============================================================

struct JSBridge::Impl {
    V8Engine* engine{nullptr};
    
    // 동기 함수 맵
    struct RegisteredFunction {
        BridgeFunction sync_func;
        AsyncBridgeFunction async_func;
        BridgeFunctionInfo info;
    };
    std::unordered_map<std::string, RegisteredFunction> functions;
    
    // 이벤트 리스너
    struct ListenerEntry {
        uint64_t id;
        std::string event;
        EventListener callback;
    };
    std::vector<ListenerEntry> listeners;
    uint64_t next_listener_id{1};
    
    // 권한 맵: extension_id → permission level
    std::unordered_map<std::string, BridgePermission> permissions;
    
    mutable std::mutex mutex;
};

// ============================================================
// 생성자/소멸자
// ============================================================

JSBridge::JSBridge(V8Engine* engine)
    : impl_(std::make_unique<Impl>())
{
    impl_->engine = engine;
    std::cout << "[JSBridge] 브릿지 초기화 완료"
              << (engine ? "" : " (V8 없음 — 스텁 모드)")
              << std::endl;
}

JSBridge::~JSBridge() {
    std::cout << "[JSBridge] 브릿지 종료 — "
              << impl_->functions.size() << "개 함수, "
              << impl_->listeners.size() << "개 리스너 해제"
              << std::endl;
}

// ============================================================
// 함수 등록
// ============================================================

void JSBridge::registerFunction(
    const std::string& name,
    BridgeFunction func,
    BridgePermission permission,
    const std::string& description
) {
    std::lock_guard lock(impl_->mutex);
    
    Impl::RegisteredFunction entry;
    entry.sync_func = std::move(func);
    entry.info.name = name;
    entry.info.description = description;
    entry.info.permission = permission;
    entry.info.is_async = false;
    
    impl_->functions[name] = std::move(entry);
    
    // V8 엔진에도 등록 (있는 경우)
    if (impl_->engine) {
        impl_->engine->registerNativeFunction(name, 
            [this, name](const std::vector<std::string>& args) -> std::string {
                std::vector<BridgeValue> bridge_args;
                for (const auto& arg : args) {
                    bridge_args.push_back(arg);
                }
                CallContext ctx;
                ctx.caller_id = "v8";
                ctx.caller_permission = BridgePermission::Extension;
                
                auto result = callFunction(name, bridge_args, ctx);
                if (result.success) {
                    if (auto* str = std::get_if<std::string>(&result.value)) {
                        return *str;
                    }
                    return "undefined";
                }
                return "Error: " + result.error;
            }
        );
    }
    
    std::cout << "[JSBridge] 함수 등록: " << name << std::endl;
}

void JSBridge::registerAsyncFunction(
    const std::string& name,
    AsyncBridgeFunction func,
    BridgePermission permission,
    const std::string& description
) {
    std::lock_guard lock(impl_->mutex);
    
    Impl::RegisteredFunction entry;
    entry.async_func = std::move(func);
    entry.info.name = name;
    entry.info.description = description;
    entry.info.permission = permission;
    entry.info.is_async = true;
    
    impl_->functions[name] = std::move(entry);
    
    std::cout << "[JSBridge] 비동기 함수 등록: " << name << std::endl;
}

void JSBridge::unregisterFunction(const std::string& name) {
    std::lock_guard lock(impl_->mutex);
    impl_->functions.erase(name);
    std::cout << "[JSBridge] 함수 해제: " << name << std::endl;
}

// ============================================================
// 함수 호출
// ============================================================

BridgeResult JSBridge::callFunction(
    const std::string& name,
    const std::vector<BridgeValue>& args,
    const CallContext& context
) {
    std::lock_guard lock(impl_->mutex);
    
    // 함수 검색
    auto it = impl_->functions.find(name);
    if (it == impl_->functions.end()) {
        return {false, nullptr, "함수를 찾을 수 없습니다: " + name};
    }
    
    auto& func = it->second;
    
    // 권한 확인
    if (!checkPermission(context, func.info.permission)) {
        return {false, nullptr, "권한 부족: " + name + " (필요: " +
            std::to_string(static_cast<int>(func.info.permission)) + ")"};
    }
    
    // 호출 시간 측정
    auto start = std::chrono::steady_clock::now();
    
    BridgeResult result;
    
    if (func.info.is_async && func.async_func) {
        // 비동기 함수 — 동기적으로 래핑 (블로킹)
        // TODO: 비동기 Promise 기반 처리
        bool completed = false;
        func.async_func(args, [&result, &completed](BridgeResult r) {
            result = std::move(r);
            completed = true;
        });
        
        if (!completed) {
            result = {false, nullptr, "비동기 함수가 즉시 완료되지 않았습니다"};
        }
    } else if (func.sync_func) {
        result = func.sync_func(args);
    } else {
        result = {false, nullptr, "함수 구현이 없습니다: " + name};
    }
    
    // 통계 업데이트
    auto end = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();
    func.info.call_count++;
    func.info.total_time_ms += elapsed_ms;
    
    return result;
}

bool JSBridge::hasFunction(const std::string& name) const {
    std::lock_guard lock(impl_->mutex);
    return impl_->functions.count(name) > 0;
}

std::vector<BridgeFunctionInfo> JSBridge::listFunctions() const {
    std::lock_guard lock(impl_->mutex);
    
    std::vector<BridgeFunctionInfo> result;
    result.reserve(impl_->functions.size());
    
    for (const auto& [name, func] : impl_->functions) {
        result.push_back(func.info);
    }
    
    return result;
}

// ============================================================
// 이벤트 시스템
// ============================================================

void JSBridge::emitEvent(const std::string& event, const BridgeValue& data) {
    std::lock_guard lock(impl_->mutex);
    
    for (const auto& listener : impl_->listeners) {
        if (listener.event == event || listener.event == "*") {
            try {
                listener.callback(event, data);
            } catch (const std::exception& e) {
                std::cerr << "[JSBridge] 이벤트 리스너 에러 (" << event << "): "
                          << e.what() << std::endl;
            }
        }
    }
    
    // V8 엔진으로 이벤트 전달 (있는 경우)
    if (impl_->engine) {
        // JavaScript의 이벤트 핸들러 호출
        std::string js_code = "if (typeof __ordinal_emit === 'function') { "
            "__ordinal_emit('" + event + "'); }";
        impl_->engine->executeScript(js_code, "<bridge-event>");
    }
}

uint64_t JSBridge::addEventListener(const std::string& event, EventListener listener) {
    std::lock_guard lock(impl_->mutex);
    
    uint64_t id = impl_->next_listener_id++;
    impl_->listeners.push_back({id, event, std::move(listener)});
    
    return id;
}

void JSBridge::removeEventListener(uint64_t listener_id) {
    std::lock_guard lock(impl_->mutex);
    
    impl_->listeners.erase(
        std::remove_if(impl_->listeners.begin(), impl_->listeners.end(),
            [listener_id](const Impl::ListenerEntry& entry) {
                return entry.id == listener_id;
            }),
        impl_->listeners.end()
    );
}

size_t JSBridge::listenerCount(const std::string& event) const {
    std::lock_guard lock(impl_->mutex);
    
    return std::count_if(impl_->listeners.begin(), impl_->listeners.end(),
        [&event](const Impl::ListenerEntry& entry) {
            return entry.event == event || entry.event == "*";
        });
}

// ============================================================
// 권한 관리
// ============================================================

bool JSBridge::checkPermission(
    const CallContext& context,
    BridgePermission required
) const {
    // Public 함수는 누구나 호출 가능
    if (required == BridgePermission::Public) {
        return true;
    }
    
    // Internal은 항상 차단 (외부 호출 불가)
    if (required == BridgePermission::Internal &&
        context.caller_id != "internal") {
        return false;
    }
    
    // 호출자 권한이 필요 권한 이상인지 확인
    if (static_cast<int>(context.caller_permission) <= static_cast<int>(required)) {
        return true;
    }
    
    // 확장 프로그램별 권한 확인
    auto it = impl_->permissions.find(context.caller_id);
    if (it != impl_->permissions.end()) {
        return static_cast<int>(it->second) <= static_cast<int>(required);
    }
    
    return false;
}

void JSBridge::grantPermission(
    const std::string& extension_id,
    BridgePermission permission
) {
    std::lock_guard lock(impl_->mutex);
    impl_->permissions[extension_id] = permission;
    std::cout << "[JSBridge] 권한 부여: " << extension_id
              << " → 레벨 " << static_cast<int>(permission) << std::endl;
}

void JSBridge::revokePermission(const std::string& extension_id) {
    std::lock_guard lock(impl_->mutex);
    impl_->permissions.erase(extension_id);
    std::cout << "[JSBridge] 권한 회수: " << extension_id << std::endl;
}

// ============================================================
// 내장 API 등록
// ============================================================

void JSBridge::registerBuiltinAPIs() {
    // ordinal.version() — 브라우저 버전
    registerFunction("ordinal.version",
        [](const std::vector<BridgeValue>&) -> BridgeResult {
            return {true, std::string("1.1.0"), ""};
        },
        BridgePermission::Public,
        "브라우저 버전 조회"
    );
    
    // ordinal.platform() — 플랫폼 정보
    registerFunction("ordinal.platform",
        [](const std::vector<BridgeValue>&) -> BridgeResult {
#if defined(__APPLE__)
            return {true, std::string("macOS"), ""};
#elif defined(_WIN32)
            return {true, std::string("Windows"), ""};
#elif defined(__linux__)
            return {true, std::string("Linux"), ""};
#else
            return {true, std::string("Unknown"), ""};
#endif
        },
        BridgePermission::Public,
        "플랫폼 정보 조회"
    );
    
    // ordinal.runtime.id — 런타임 식별자
    registerFunction("ordinal.runtime.id",
        [](const std::vector<BridgeValue>&) -> BridgeResult {
            return {true, std::string("ordinalv8-runtime"), ""};
        },
        BridgePermission::Extension,
        "런타임 식별자 조회"
    );
    
    // ordinal.runtime.getManifest — 매니페스트 조회
    registerFunction("ordinal.runtime.getManifest",
        [](const std::vector<BridgeValue>&) -> BridgeResult {
            std::unordered_map<std::string, std::string> manifest;
            manifest["name"] = "OrdinalV8";
            manifest["version"] = "1.1.0";
            manifest["manifest_version"] = "3";
            return {true, manifest, ""};
        },
        BridgePermission::Extension,
        "확장 매니페스트 조회"
    );
    
    // ordinal.storage.local.get — 로컬 스토리지 읽기
    registerFunction("ordinal.storage.local.get",
        [](const std::vector<BridgeValue>& args) -> BridgeResult {
            if (args.empty()) {
                return {false, nullptr, "키가 필요합니다"};
            }
            // TODO: 실제 스토리지 구현과 연결
            return {true, std::string("{}"), ""};
        },
        BridgePermission::Extension,
        "로컬 스토리지에서 값 읽기"
    );
    
    // ordinal.storage.local.set — 로컬 스토리지 쓰기
    registerFunction("ordinal.storage.local.set",
        [](const std::vector<BridgeValue>& args) -> BridgeResult {
            if (args.empty()) {
                return {false, nullptr, "저장할 데이터가 필요합니다"};
            }
            // TODO: 실제 스토리지 구현과 연결
            return {true, nullptr, ""};
        },
        BridgePermission::Extension,
        "로컬 스토리지에 값 쓰기"
    );
    
    // ordinal.tabs.query — 탭 목록 조회
    registerFunction("ordinal.tabs.query",
        [](const std::vector<BridgeValue>&) -> BridgeResult {
            // TODO: 실제 탭 관리자와 연결
            return {true, std::string("[]"), ""};
        },
        BridgePermission::Extension,
        "탭 목록 조회 (쿼리 필터 지원)"
    );
    
    // ordinal.tabs.create — 새 탭 생성
    registerFunction("ordinal.tabs.create",
        [](const std::vector<BridgeValue>& args) -> BridgeResult {
            if (args.empty()) {
                return {false, nullptr, "URL이 필요합니다"};
            }
            // TODO: 실제 탭 생성
            return {true, std::string("{\"id\": 0}"), ""};
        },
        BridgePermission::Extension,
        "새 탭 생성"
    );
    
    // ordinal.notifications.create — 알림 생성
    registerFunction("ordinal.notifications.create",
        [](const std::vector<BridgeValue>& args) -> BridgeResult {
            if (args.empty()) {
                return {false, nullptr, "알림 데이터가 필요합니다"};
            }
            // TODO: 시스템 알림 연결
            return {true, std::string("notification-0"), ""};
        },
        BridgePermission::Extension,
        "시스템 알림 생성"
    );
    
    std::cout << "[JSBridge] 내장 API " << impl_->functions.size()
              << "개 등록 완료" << std::endl;
}

// ============================================================
// 통계
// ============================================================

std::unordered_map<std::string, BridgeFunctionInfo> JSBridge::getStats() const {
    std::lock_guard lock(impl_->mutex);
    
    std::unordered_map<std::string, BridgeFunctionInfo> stats;
    for (const auto& [name, func] : impl_->functions) {
        stats[name] = func.info;
    }
    
    return stats;
}

void JSBridge::resetStats() {
    std::lock_guard lock(impl_->mutex);
    
    for (auto& [name, func] : impl_->functions) {
        func.info.call_count = 0;
        func.info.total_time_ms = 0;
    }
}

} // namespace ordinal::core
