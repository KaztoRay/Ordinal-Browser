#pragma once

/**
 * @file request_interceptor.h
 * @brief 네트워크 요청 인터셉터 (보안 검사 미들웨어)
 * 
 * 모든 네트워크 요청을 가로채서 보안 검사를 수행합니다.
 * 피싱 URL 차단, 악성 스크립트 탐지, 추적기 차단 등의 미들웨어 체인을 관리합니다.
 */

#include "http_client.h"

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <chrono>
#include <atomic>

namespace ordinal::network {

/**
 * @brief 인터셉트 결과
 */
enum class InterceptAction {
    Allow,          ///< 요청 허용
    Block,          ///< 요청 차단
    Modify,         ///< 요청 수정 후 허용
    Redirect,       ///< 다른 URL로 리다이렉트
    Delay           ///< 지연 후 허용 (추가 분석 필요)
};

/**
 * @brief 차단 이유 카테고리
 */
enum class BlockReason {
    None,
    Phishing,           ///< 피싱 사이트
    Malware,            ///< 악성코드
    Tracker,            ///< 추적기
    MixedContent,       ///< 혼합 콘텐츠 (HTTPS에서 HTTP)
    InvalidCertificate, ///< 잘못된 인증서
    Blacklisted,        ///< 블랙리스트
    XssAttempt,         ///< XSS 시도 감지
    UserDefined         ///< 사용자 정의 규칙
};

/**
 * @brief 인터셉트 결과 구조체
 */
struct InterceptResult {
    InterceptAction action{InterceptAction::Allow};
    BlockReason reason{BlockReason::None};
    std::string reason_detail;          ///< 상세 차단 사유
    std::string redirect_url;           ///< 리다이렉트 URL (action이 Redirect일 때)
    HttpRequest modified_request;       ///< 수정된 요청 (action이 Modify일 때)
    double analysis_time_ms{0.0};       ///< 분석 소요 시간
};

/**
 * @brief 인터셉터 미들웨어 인터페이스
 * 
 * 요청/응답 파이프라인에 삽입되는 미들웨어의 기본 클래스입니다.
 */
class InterceptMiddleware {
public:
    virtual ~InterceptMiddleware() = default;

    /**
     * @brief 요청 인터셉트 (요청 전 검사)
     * @param request HTTP 요청
     * @return 인터셉트 결과
     */
    [[nodiscard]] virtual InterceptResult onRequest(const HttpRequest& request) = 0;

    /**
     * @brief 응답 인터셉트 (응답 후 검사)
     * @param request 원본 요청
     * @param response HTTP 응답
     * @return 인터셉트 결과
     */
    [[nodiscard]] virtual InterceptResult onResponse(
        const HttpRequest& request,
        const HttpResponse& response
    ) = 0;

    /**
     * @brief 미들웨어 이름
     */
    [[nodiscard]] virtual std::string name() const = 0;

    /**
     * @brief 미들웨어 우선순위 (낮을수록 먼저 실행)
     */
    [[nodiscard]] virtual int priority() const { return 100; }

    /**
     * @brief 미들웨어 활성 상태
     */
    [[nodiscard]] bool isEnabled() const { return enabled_.load(); }
    void setEnabled(bool enabled) { enabled_.store(enabled); }

private:
    std::atomic<bool> enabled_{true};
};

/**
 * @brief 요청 인터셉터 (미들웨어 체인 관리자)
 * 
 * 여러 미들웨어를 체인으로 연결하여 요청/응답을 검사합니다.
 * 하나라도 차단하면 즉시 차단됩니다.
 */
class RequestInterceptor {
public:
    RequestInterceptor();
    ~RequestInterceptor();

    /**
     * @brief 미들웨어 추가
     * @param middleware 미들웨어 인스턴스
     */
    void addMiddleware(std::shared_ptr<InterceptMiddleware> middleware);

    /**
     * @brief 미들웨어 제거
     * @param name 미들웨어 이름
     */
    void removeMiddleware(const std::string& name);

    /**
     * @brief 요청 검사 (모든 미들웨어 체인 실행)
     * @param request HTTP 요청
     * @return 최종 인터셉트 결과
     */
    [[nodiscard]] InterceptResult interceptRequest(const HttpRequest& request);

    /**
     * @brief 응답 검사 (모든 미들웨어 체인 실행)
     * @param request 원본 요청
     * @param response HTTP 응답
     * @return 최종 인터셉트 결과
     */
    [[nodiscard]] InterceptResult interceptResponse(
        const HttpRequest& request,
        const HttpResponse& response
    );

    // ============================
    // 통계
    // ============================

    /**
     * @brief 총 검사된 요청 수
     */
    [[nodiscard]] uint64_t totalRequestsInspected() const { return total_inspected_.load(); }

    /**
     * @brief 총 차단된 요청 수
     */
    [[nodiscard]] uint64_t totalRequestsBlocked() const { return total_blocked_.load(); }

    /**
     * @brief 통계 초기화
     */
    void resetStats();

    /**
     * @brief 등록된 미들웨어 수
     */
    [[nodiscard]] size_t middlewareCount() const { return middlewares_.size(); }

private:
    std::vector<std::shared_ptr<InterceptMiddleware>> middlewares_;
    std::atomic<uint64_t> total_inspected_{0};
    std::atomic<uint64_t> total_blocked_{0};

    /**
     * @brief 미들웨어를 우선순위 순으로 정렬
     */
    void sortMiddlewares();
};

} // namespace ordinal::network
