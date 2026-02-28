#pragma once

/**
 * @file js_bridge.h
 * @brief JavaScript ↔ C++ 브릿지 — V8 바인딩 레이어
 * 
 * C++ 네이티브 API를 JavaScript에 노출하고,
 * JavaScript 이벤트를 C++로 전달하는 양방향 브릿지를 제공합니다.
 * 
 * 기능:
 * - 네이티브 함수를 JavaScript에서 호출 가능하게 등록
 * - C++ → JavaScript 이벤트 발생
 * - JavaScript → C++ 이벤트 리스너
 * - 권한 기반 접근 제어 (확장 프로그램 API 보안)
 * - 동기/비동기 호출 지원
 */

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <variant>
#include <optional>
#include <mutex>
#include <any>
#include <chrono>

namespace ordinal::core {

// 전방 선언
class V8Engine;

/**
 * @brief 브릿지 값 타입 — JavaScript ↔ C++ 값 교환
 */
using BridgeValue = std::variant<
    std::nullptr_t,             // null/undefined
    bool,                       // boolean
    int64_t,                    // integer
    double,                     // number
    std::string,                // string
    std::vector<std::string>,   // string array (단순화)
    std::unordered_map<std::string, std::string>  // object (key-value)
>;

/**
 * @brief 브릿지 호출 결과
 */
struct BridgeResult {
    bool success;                   ///< 성공 여부
    BridgeValue value;              ///< 반환값
    std::string error;              ///< 에러 메시지 (실패 시)
};

/**
 * @brief 브릿지 함수 시그니처
 * 
 * JavaScript에서 호출되는 네이티브 함수의 타입입니다.
 * 인자는 BridgeValue 배열로 전달되고, 결과는 BridgeResult로 반환됩니다.
 */
using BridgeFunction = std::function<BridgeResult(const std::vector<BridgeValue>&)>;

/**
 * @brief 비동기 브릿지 함수 시그니처
 * 
 * 완료 시 콜백으로 결과를 반환합니다.
 */
using AsyncBridgeFunction = std::function<void(
    const std::vector<BridgeValue>&,
    std::function<void(BridgeResult)>
)>;

/**
 * @brief 이벤트 리스너 콜백
 */
using EventListener = std::function<void(const std::string& event, const BridgeValue& data)>;

/**
 * @brief 브릿지 함수 권한 레벨
 */
enum class BridgePermission {
    Public,         ///< 모든 코드에서 호출 가능
    Extension,      ///< 확장 프로그램만 호출 가능
    Privileged,     ///< 특권 확장만 호출 가능 (browser.* API)
    Internal        ///< 내부 코드만 호출 가능
};

/**
 * @brief 등록된 브릿지 함수 정보
 */
struct BridgeFunctionInfo {
    std::string name;                   ///< 함수 이름 (네임스페이스 포함)
    std::string description;            ///< 설명
    BridgePermission permission;        ///< 필요 권한
    bool is_async;                      ///< 비동기 여부
    std::vector<std::string> param_names; ///< 파라미터 이름 목록
    
    // 통계
    uint64_t call_count{0};             ///< 호출 횟수
    double total_time_ms{0};            ///< 총 실행 시간 (ms)
};

/**
 * @brief 호출 컨텍스트 — 호출자 정보
 */
struct CallContext {
    std::string caller_id;              ///< 호출자 식별자 (확장 ID, "page", "internal")
    std::string origin;                 ///< 호출 출처 URL
    BridgePermission caller_permission; ///< 호출자 권한 레벨
    std::chrono::steady_clock::time_point timestamp; ///< 호출 시간
};

/**
 * @brief JavaScript ↔ C++ 브릿지
 * 
 * V8 엔진과 C++ 네이티브 코드 사이의 양방향 통신을 관리합니다.
 * 확장 프로그램의 browser.* API와 웹 페이지에 노출되는 네이티브 함수를 등록합니다.
 */
class JSBridge {
public:
    /**
     * @brief 생성자
     * @param engine V8 엔진 인스턴스 (null 허용 — 스텁 모드)
     */
    explicit JSBridge(V8Engine* engine = nullptr);
    ~JSBridge();

    // 복사 금지
    JSBridge(const JSBridge&) = delete;
    JSBridge& operator=(const JSBridge&) = delete;

    // ---- 동기 함수 등록 ----

    /**
     * @brief 동기 네이티브 함수 등록
     * @param name 함수 이름 (예: "ordinal.tabs.query")
     * @param func 네이티브 함수
     * @param permission 필요 권한
     * @param description 함수 설명
     */
    void registerFunction(
        const std::string& name,
        BridgeFunction func,
        BridgePermission permission = BridgePermission::Public,
        const std::string& description = ""
    );

    /**
     * @brief 비동기 네이티브 함수 등록
     * @param name 함수 이름
     * @param func 비동기 네이티브 함수
     * @param permission 필요 권한
     * @param description 함수 설명
     */
    void registerAsyncFunction(
        const std::string& name,
        AsyncBridgeFunction func,
        BridgePermission permission = BridgePermission::Public,
        const std::string& description = ""
    );

    /**
     * @brief 등록된 함수 해제
     * @param name 함수 이름
     */
    void unregisterFunction(const std::string& name);

    // ---- 함수 호출 ----

    /**
     * @brief 등록된 네이티브 함수 호출
     * @param name 함수 이름
     * @param args 인자 목록
     * @param context 호출 컨텍스트
     * @return 호출 결과
     */
    BridgeResult callFunction(
        const std::string& name,
        const std::vector<BridgeValue>& args,
        const CallContext& context = {}
    );

    /**
     * @brief 함수 존재 여부 확인
     */
    [[nodiscard]] bool hasFunction(const std::string& name) const;

    /**
     * @brief 등록된 함수 목록 조회
     */
    [[nodiscard]] std::vector<BridgeFunctionInfo> listFunctions() const;

    // ---- 이벤트 시스템 (C++ → JS) ----

    /**
     * @brief JavaScript로 이벤트 발생
     * @param event 이벤트 이름 (예: "tabs.onCreated")
     * @param data 이벤트 데이터
     */
    void emitEvent(const std::string& event, const BridgeValue& data);

    /**
     * @brief 이벤트 리스너 등록 (JS → C++)
     * @param event 이벤트 이름
     * @param listener 콜백 함수
     * @return 리스너 ID (해제용)
     */
    uint64_t addEventListener(const std::string& event, EventListener listener);

    /**
     * @brief 이벤트 리스너 해제
     * @param listener_id 리스너 ID
     */
    void removeEventListener(uint64_t listener_id);

    /**
     * @brief 특정 이벤트의 리스너 수 조회
     */
    [[nodiscard]] size_t listenerCount(const std::string& event) const;

    // ---- 권한 관리 ----

    /**
     * @brief 호출자의 권한 확인
     * @param context 호출 컨텍스트
     * @param required 필요 권한
     * @return 권한 충족 여부
     */
    [[nodiscard]] bool checkPermission(
        const CallContext& context,
        BridgePermission required
    ) const;

    /**
     * @brief 확장 프로그램에 권한 부여
     * @param extension_id 확장 ID
     * @param permission 부여할 권한
     */
    void grantPermission(const std::string& extension_id, BridgePermission permission);

    /**
     * @brief 확장 프로그램 권한 회수
     */
    void revokePermission(const std::string& extension_id);

    // ---- 내장 API 등록 ----

    /**
     * @brief 기본 OrdinalV8 API 등록
     * 
     * 다음 API를 자동 등록합니다:
     * - ordinal.version() — 브라우저 버전
     * - ordinal.platform() — 플랫폼 정보
     * - ordinal.tabs.* — 탭 관리 API
     * - ordinal.storage.* — 스토리지 API
     * - ordinal.runtime.* — 런타임 API
     */
    void registerBuiltinAPIs();

    // ---- 통계 ----

    /**
     * @brief 브릿지 호출 통계 조회
     */
    [[nodiscard]] std::unordered_map<std::string, BridgeFunctionInfo> getStats() const;

    /**
     * @brief 통계 초기화
     */
    void resetStats();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ordinal::core
