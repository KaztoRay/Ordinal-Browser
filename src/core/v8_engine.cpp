/**
 * @file v8_engine.cpp
 * @brief V8 JavaScript 엔진 래퍼 구현
 * 
 * V8 엔진의 초기화, 컨텍스트 관리, 스크립트 실행 로직을 구현합니다.
 */

#include "v8_engine.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <cassert>

namespace ordinal::core {

// 정적 멤버 초기화
std::unique_ptr<v8::Platform> V8Engine::s_platform_ = nullptr;
bool V8Engine::s_initialized_ = false;

/**
 * @brief V8Engine 내부 구현 (PIMPL)
 * V8 헤더 의존성을 .cpp 파일로 격리합니다.
 */
struct V8Engine::Impl {
    V8EngineConfig config;

#ifdef V8_COMPRESS_POINTERS
    v8::Isolate* isolate{nullptr};
    v8::Global<v8::Context> context;
    v8::Isolate::CreateParams create_params;
#endif

    bool context_initialized{false};
};

// ============================================================
// 전역 초기화/종료
// ============================================================

bool V8Engine::initialize(const V8EngineConfig& config) {
    if (s_initialized_) {
        std::cerr << "[V8Engine] 이미 초기화되어 있습니다." << std::endl;
        return true;
    }

#ifdef V8_COMPRESS_POINTERS
    // ICU 데이터 초기화
    if (!config.icu_data_path.empty()) {
        v8::V8::InitializeICUDefaultLocation(config.icu_data_path.c_str());
    }

    // 스냅샷 초기화
    if (!config.snapshot_blob_path.empty()) {
        v8::V8::InitializeExternalStartupDataFromDirectory(
            config.snapshot_blob_path.c_str()
        );
    }

    // V8 플랫폼 생성 (스레드 풀 기반)
    s_platform_ = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(s_platform_.get());

    // V8 엔진 초기화
    if (!v8::V8::Initialize()) {
        std::cerr << "[V8Engine] V8 초기화 실패!" << std::endl;
        return false;
    }

    // 선택적 플래그 설정
    if (config.enable_harmony) {
        v8::V8::SetFlagsFromString("--harmony");
    }
    if (config.enable_wasm) {
        v8::V8::SetFlagsFromString("--experimental-wasm-gc");
    }
#endif

    s_initialized_ = true;
    std::cout << "[V8Engine] V8 엔진 초기화 완료" << std::endl;
    return true;
}

void V8Engine::shutdown() {
    if (!s_initialized_) return;

#ifdef V8_COMPRESS_POINTERS
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
#endif

    s_platform_.reset();
    s_initialized_ = false;
    std::cout << "[V8Engine] V8 엔진 종료 완료" << std::endl;
}

bool V8Engine::isInitialized() {
    return s_initialized_;
}

// ============================================================
// 생성자/소멸자
// ============================================================

V8Engine::V8Engine(const V8EngineConfig& config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = config;

    if (!s_initialized_) {
        // 자동 전역 초기화 (아직 안 되어 있으면)
        initialize(config);
    }

#ifdef V8_COMPRESS_POINTERS
    // ArrayBuffer 할당자 생성
    impl_->create_params.array_buffer_allocator =
        v8::ArrayBuffer::Allocator::NewDefaultAllocator();

    // 힙 크기 제한 설정
    impl_->create_params.constraints.set_max_old_generation_size_in_bytes(
        config.max_old_space_mb * 1024 * 1024
    );

    // Isolate 생성 (독립 실행 환경)
    impl_->isolate = v8::Isolate::New(impl_->create_params);

    // 기본 컨텍스트 생성
    {
        v8::Isolate::Scope isolate_scope(impl_->isolate);
        v8::HandleScope handle_scope(impl_->isolate);

        // 전역 객체 템플릿
        v8::Local<v8::ObjectTemplate> global_template =
            v8::ObjectTemplate::New(impl_->isolate);

        // console.log 바인딩
        // TODO: 더 많은 네이티브 함수 바인딩 추가

        v8::Local<v8::Context> context =
            v8::Context::New(impl_->isolate, nullptr, global_template);
        impl_->context.Reset(impl_->isolate, context);
    }

    impl_->context_initialized = true;
#else
    // V8 없이 빌드 - 더미 모드
    impl_->context_initialized = true;
    std::cout << "[V8Engine] V8 없이 빌드됨 (더미 모드)" << std::endl;
#endif
}

V8Engine::~V8Engine() {
#ifdef V8_COMPRESS_POINTERS
    if (impl_ && impl_->isolate) {
        impl_->context.Reset();
        impl_->isolate->Dispose();
        delete impl_->create_params.array_buffer_allocator;
    }
#endif
}

V8Engine::V8Engine(V8Engine&& other) noexcept = default;
V8Engine& V8Engine::operator=(V8Engine&& other) noexcept = default;

// ============================================================
// 스크립트 실행
// ============================================================

JsResult V8Engine::executeScript(
    const std::string& source_code,
    const std::string& source_name
) {
    JsResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

#ifdef V8_COMPRESS_POINTERS
    if (!impl_->isolate || !impl_->context_initialized) {
        result.success = false;
        result.error_message = "V8 엔진이 초기화되지 않았습니다.";
        return result;
    }

    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);
    v8::Local<v8::Context> context =
        v8::Local<v8::Context>::New(impl_->isolate, impl_->context);
    v8::Context::Scope context_scope(context);

    // TryCatch로 예외 처리
    v8::TryCatch try_catch(impl_->isolate);

    // 소스 코드를 V8 문자열로 변환
    v8::Local<v8::String> source =
        v8::String::NewFromUtf8(impl_->isolate, source_code.c_str(),
                                v8::NewStringType::kNormal).ToLocalChecked();

    v8::Local<v8::String> name =
        v8::String::NewFromUtf8(impl_->isolate, source_name.c_str(),
                                v8::NewStringType::kNormal).ToLocalChecked();

    // 스크립트 컴파일
    v8::ScriptOrigin origin(name);
    v8::MaybeLocal<v8::Script> maybe_script =
        v8::Script::Compile(context, source, &origin);

    if (maybe_script.IsEmpty()) {
        // 컴파일 에러
        result.success = false;
        if (try_catch.HasCaught()) {
            v8::Local<v8::Message> message = try_catch.Message();
            v8::String::Utf8Value error_msg(impl_->isolate, try_catch.Exception());
            result.error_message = *error_msg ? *error_msg : "알 수 없는 컴파일 오류";
            result.line_number = message->GetLineNumber(context).FromMaybe(0);
        }
        return result;
    }

    // 스크립트 실행
    v8::Local<v8::Script> script = maybe_script.ToLocalChecked();
    v8::MaybeLocal<v8::Value> maybe_result = script->Run(context);

    if (maybe_result.IsEmpty()) {
        // 런타임 에러
        result.success = false;
        if (try_catch.HasCaught()) {
            v8::String::Utf8Value error_msg(impl_->isolate, try_catch.Exception());
            result.error_message = *error_msg ? *error_msg : "알 수 없는 런타임 오류";

            v8::Local<v8::Message> message = try_catch.Message();
            if (!message.IsEmpty()) {
                result.line_number = message->GetLineNumber(context).FromMaybe(0);
            }
        }
    } else {
        // 성공
        result.success = true;
        v8::Local<v8::Value> val = maybe_result.ToLocalChecked();
        v8::String::Utf8Value utf8(impl_->isolate, val);
        result.value = *utf8 ? *utf8 : "undefined";
    }
#else
    // V8 없이 빌드 - 더미 모드 (코드 구문만 기록)
    result.success = true;
    result.value = "[더미 모드] 스크립트 실행 건너뜀: " + source_name;
    std::cout << "[V8Engine] 더미 모드 - 스크립트: " << source_name << std::endl;
#endif

    auto end_time = std::chrono::high_resolution_clock::now();
    result.execution_time_ms = std::chrono::duration<double, std::milli>(
        end_time - start_time
    ).count();

    return result;
}

std::optional<std::string> V8Engine::validateScript(const std::string& source_code) {
#ifdef V8_COMPRESS_POINTERS
    if (!impl_->isolate) {
        return "V8 엔진이 초기화되지 않았습니다.";
    }

    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);
    v8::Local<v8::Context> context =
        v8::Local<v8::Context>::New(impl_->isolate, impl_->context);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(impl_->isolate);

    v8::Local<v8::String> source =
        v8::String::NewFromUtf8(impl_->isolate, source_code.c_str(),
                                v8::NewStringType::kNormal).ToLocalChecked();

    v8::ScriptOrigin origin(
        v8::String::NewFromUtf8Literal(impl_->isolate, "<validation>"));

    v8::MaybeLocal<v8::Script> maybe_script =
        v8::Script::Compile(context, source, &origin);

    if (maybe_script.IsEmpty() && try_catch.HasCaught()) {
        v8::String::Utf8Value error(impl_->isolate, try_catch.Exception());
        return std::string(*error ? *error : "구문 오류");
    }

    return std::nullopt; // 유효한 스크립트
#else
    // 더미 모드에서는 항상 유효
    (void)source_code;
    return std::nullopt;
#endif
}

// ============================================================
// 네이티브 함수/변수 바인딩
// ============================================================

void V8Engine::registerNativeFunction(
    const std::string& name,
    NativeFunction func
) {
    native_functions_[name] = std::move(func);

#ifdef V8_COMPRESS_POINTERS
    // V8 컨텍스트에 함수 등록
    if (!impl_->isolate) return;

    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);
    v8::Local<v8::Context> context =
        v8::Local<v8::Context>::New(impl_->isolate, impl_->context);
    v8::Context::Scope context_scope(context);

    // TODO: v8::FunctionTemplate을 사용하여 실제 바인딩 구현
    // 네이티브 함수를 JavaScript에서 호출 가능하게 만들기
#endif

    std::cout << "[V8Engine] 네이티브 함수 등록: " << name << std::endl;
}

void V8Engine::setGlobalVariable(
    const std::string& name,
    const std::string& json_value
) {
#ifdef V8_COMPRESS_POINTERS
    if (!impl_->isolate) return;

    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);
    v8::Local<v8::Context> context =
        v8::Local<v8::Context>::New(impl_->isolate, impl_->context);
    v8::Context::Scope context_scope(context);

    // JSON 파싱하여 전역 변수로 설정
    v8::Local<v8::String> json_str =
        v8::String::NewFromUtf8(impl_->isolate, json_value.c_str(),
                                v8::NewStringType::kNormal).ToLocalChecked();

    v8::MaybeLocal<v8::Value> parsed = v8::JSON::Parse(context, json_str);
    if (!parsed.IsEmpty()) {
        v8::Local<v8::String> var_name =
            v8::String::NewFromUtf8(impl_->isolate, name.c_str(),
                                    v8::NewStringType::kNormal).ToLocalChecked();
        context->Global()->Set(context, var_name, parsed.ToLocalChecked()).Check();
    }
#else
    std::cout << "[V8Engine] 더미 모드 - 전역 변수 설정: " << name << " = " << json_value << std::endl;
    (void)name;
    (void)json_value;
#endif
}

std::optional<std::string> V8Engine::getGlobalVariable(const std::string& name) {
#ifdef V8_COMPRESS_POINTERS
    if (!impl_->isolate) return std::nullopt;

    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);
    v8::Local<v8::Context> context =
        v8::Local<v8::Context>::New(impl_->isolate, impl_->context);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::String> var_name =
        v8::String::NewFromUtf8(impl_->isolate, name.c_str(),
                                v8::NewStringType::kNormal).ToLocalChecked();

    v8::MaybeLocal<v8::Value> maybe_val = context->Global()->Get(context, var_name);
    if (maybe_val.IsEmpty()) return std::nullopt;

    v8::Local<v8::Value> val = maybe_val.ToLocalChecked();
    if (val->IsUndefined()) return std::nullopt;

    v8::MaybeLocal<v8::String> json_str = v8::JSON::Stringify(context, val);
    if (json_str.IsEmpty()) return std::nullopt;

    v8::String::Utf8Value utf8(impl_->isolate, json_str.ToLocalChecked());
    return std::string(*utf8 ? *utf8 : "null");
#else
    (void)name;
    return std::nullopt;
#endif
}

// ============================================================
// 유틸리티
// ============================================================

void V8Engine::resetContext() {
#ifdef V8_COMPRESS_POINTERS
    if (!impl_->isolate) return;

    v8::Isolate::Scope isolate_scope(impl_->isolate);
    v8::HandleScope handle_scope(impl_->isolate);

    // 기존 컨텍스트 해제
    impl_->context.Reset();

    // 새 컨텍스트 생성
    v8::Local<v8::ObjectTemplate> global_template =
        v8::ObjectTemplate::New(impl_->isolate);

    v8::Local<v8::Context> new_context =
        v8::Context::New(impl_->isolate, nullptr, global_template);
    impl_->context.Reset(impl_->isolate, new_context);

    // 등록된 네이티브 함수 재적용
    for (const auto& [name, func] : native_functions_) {
        registerNativeFunction(name, func);
    }
#endif
    std::cout << "[V8Engine] 컨텍스트 초기화 완료" << std::endl;
}

size_t V8Engine::getHeapUsedBytes() const {
#ifdef V8_COMPRESS_POINTERS
    if (!impl_->isolate) return 0;

    v8::HeapStatistics stats;
    impl_->isolate->GetHeapStatistics(&stats);
    return stats.used_heap_size();
#else
    return 0;
#endif
}

void V8Engine::collectGarbage() {
#ifdef V8_COMPRESS_POINTERS
    if (!impl_->isolate) return;

    impl_->isolate->LowMemoryNotification();
    std::cout << "[V8Engine] 가비지 컬렉션 실행 완료" << std::endl;
#endif
}

void V8Engine::terminateExecution() {
#ifdef V8_COMPRESS_POINTERS
    if (!impl_->isolate) return;

    impl_->isolate->TerminateExecution();
    std::cout << "[V8Engine] 스크립트 실행 종료" << std::endl;
#endif
}

} // namespace ordinal::core
