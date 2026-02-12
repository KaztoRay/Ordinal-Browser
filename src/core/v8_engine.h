#pragma once

/**
 * @file v8_engine.h
 * @brief V8 JavaScript 엔진 래퍼 클래스
 * 
 * V8 엔진의 초기화, 컨텍스트 관리, 스크립트 실행을 담당합니다.
 * 각 탭/페이지마다 독립된 JavaScript 실행 환경을 제공합니다.
 */

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <optional>
#include <expected>
#include <variant>

// V8 헤더 (조건부 포함)
#ifdef V8_COMPRESS_POINTERS
#include <v8.h>
#include <libplatform/libplatform.h>
#else
// V8 없이 컴파일할 때 사용할 전방 선언
namespace v8 {
    class Isolate;
    class Context;
    class Platform;
    class Value;
    template<class T> class Local;
    template<class T> class Global;
    template<class T> class MaybeLocal;
}
#endif

namespace ordinal::core {

/**
 * @brief JavaScript 실행 결과를 나타내는 구조체
 */
struct JsResult {
    bool success;                       ///< 실행 성공 여부
    std::string value;                  ///< 반환값 (문자열 변환)
    std::string error_message;          ///< 에러 메시지 (실패 시)
    int line_number{0};                 ///< 에러 발생 라인 (실패 시)
    double execution_time_ms{0.0};      ///< 실행 시간 (밀리초)
};

/**
 * @brief JavaScript 실행 에러 타입
 */
enum class JsErrorType {
    None,
    SyntaxError,        ///< 구문 오류
    TypeError,          ///< 타입 오류
    ReferenceError,     ///< 참조 오류
    RangeError,         ///< 범위 오류
    SecurityError,      ///< 보안 위반 (커스텀)
    TimeoutError,       ///< 실행 시간 초과
    MemoryError,        ///< 메모리 한도 초과
    Unknown             ///< 알 수 없는 오류
};

/**
 * @brief V8 엔진 설정 구조체
 */
struct V8EngineConfig {
    size_t max_heap_size_mb{512};       ///< 최대 힙 크기 (MB)
    size_t max_old_space_mb{256};       ///< 최대 Old Space 크기 (MB)
    uint32_t script_timeout_ms{5000};   ///< 스크립트 실행 타임아웃 (밀리초)
    bool enable_wasm{false};            ///< WebAssembly 활성화 여부
    bool enable_harmony{true};          ///< ES6+ Harmony 기능 활성화
    std::string snapshot_blob_path;     ///< V8 스냅샷 경로
    std::string icu_data_path;          ///< ICU 데이터 경로
};

/**
 * @brief C++에서 JavaScript로 노출할 네이티브 함수 타입
 */
using NativeFunction = std::function<std::string(const std::vector<std::string>&)>;

/**
 * @brief V8 JavaScript 엔진 래퍼
 * 
 * V8 엔진의 수명 주기를 관리하고, JavaScript 코드 실행 인터페이스를 제공합니다.
 * 싱글톤 패턴으로 구현되며, 각 탭은 별도의 ExecutionContext를 사용합니다.
 */
class V8Engine {
public:
    /**
     * @brief V8 엔진 전역 초기화 (프로세스당 한 번)
     * @param config 엔진 설정
     * @return 초기화 성공 여부
     */
    static bool initialize(const V8EngineConfig& config = {});

    /**
     * @brief V8 엔진 전역 종료
     */
    static void shutdown();

    /**
     * @brief 전역 초기화 상태 확인
     */
    static bool isInitialized();

    // 복사/이동 금지 (Isolate는 공유 불가)
    V8Engine(const V8Engine&) = delete;
    V8Engine& operator=(const V8Engine&) = delete;

    /**
     * @brief 새 V8 Isolate(독립 실행 환경) 생성
     * @param config 설정 (기본값 사용 가능)
     */
    explicit V8Engine(const V8EngineConfig& config = {});
    ~V8Engine();

    // 이동은 허용
    V8Engine(V8Engine&& other) noexcept;
    V8Engine& operator=(V8Engine&& other) noexcept;

    /**
     * @brief JavaScript 코드 실행
     * @param source_code 실행할 JavaScript 소스 코드
     * @param source_name 소스 이름 (디버깅용, 기본값: "<anonymous>")
     * @return 실행 결과
     */
    [[nodiscard]] JsResult executeScript(
        const std::string& source_code,
        const std::string& source_name = "<anonymous>"
    );

    /**
     * @brief JavaScript 코드의 구문 유효성 검사 (실행하지 않음)
     * @param source_code 검사할 JavaScript 소스 코드
     * @return 유효한 경우 std::nullopt, 아니면 에러 메시지
     */
    [[nodiscard]] std::optional<std::string> validateScript(
        const std::string& source_code
    );

    /**
     * @brief JavaScript 전역 객체에 네이티브 함수 등록
     * @param name JavaScript에서 호출할 함수 이름
     * @param func C++ 네이티브 함수
     */
    void registerNativeFunction(const std::string& name, NativeFunction func);

    /**
     * @brief JavaScript 전역 변수 설정
     * @param name 변수 이름
     * @param json_value JSON 형식의 값
     */
    void setGlobalVariable(const std::string& name, const std::string& json_value);

    /**
     * @brief JavaScript 전역 변수 조회
     * @param name 변수 이름
     * @return JSON 형식의 값 (없으면 std::nullopt)
     */
    [[nodiscard]] std::optional<std::string> getGlobalVariable(const std::string& name);

    /**
     * @brief 현재 컨텍스트 초기화 (전역 상태 리셋)
     */
    void resetContext();

    /**
     * @brief 메모리 사용량 조회 (바이트)
     */
    [[nodiscard]] size_t getHeapUsedBytes() const;

    /**
     * @brief 가비지 컬렉션 수동 실행
     */
    void collectGarbage();

    /**
     * @brief 실행 중인 스크립트 강제 종료
     */
    void terminateExecution();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;       ///< PIMPL 패턴으로 V8 헤더 의존성 격리

    /// V8 플랫폼 (전역, 프로세스당 하나)
    static std::unique_ptr<v8::Platform> s_platform_;
    static bool s_initialized_;

    /// 등록된 네이티브 함수 맵
    std::unordered_map<std::string, NativeFunction> native_functions_;
};

} // namespace ordinal::core
