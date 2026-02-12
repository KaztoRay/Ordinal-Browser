#pragma once

/**
 * @file http_client.h
 * @brief HTTP/HTTPS 클라이언트 (libcurl 래퍼)
 * 
 * libcurl을 래핑하여 HTTP/HTTPS 요청을 처리합니다.
 * 비동기 요청, 쿠키 관리, 프록시 지원 등을 제공합니다.
 */

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <optional>
#include <chrono>
#include <future>

namespace ordinal::network {

/**
 * @brief HTTP 메서드
 */
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE_,
    PATCH,
    HEAD,
    OPTIONS
};

/**
 * @brief HTTP 요청 구조체
 */
struct HttpRequest {
    HttpMethod method{HttpMethod::GET};
    std::string url;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::string content_type;

    // 타임아웃 설정
    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds transfer_timeout{30};

    // 프록시 설정
    std::optional<std::string> proxy_url;

    // SSL 설정
    bool verify_ssl{true};
    std::optional<std::string> ca_cert_path;

    // 리다이렉션 설정
    bool follow_redirects{true};
    int max_redirects{10};

    // 쿠키 설정
    bool enable_cookies{true};
    std::optional<std::string> cookie_jar_path;
};

/**
 * @brief SSL 인증서 정보
 */
struct CertificateInfo {
    std::string subject;            ///< 인증서 주체
    std::string issuer;             ///< 발급자
    std::string serial_number;      ///< 시리얼 번호
    std::string valid_from;         ///< 유효 시작일
    std::string valid_until;        ///< 유효 종료일
    std::string fingerprint_sha256; ///< SHA-256 지문
    bool is_valid{false};           ///< 유효성
    std::vector<std::string> san;   ///< Subject Alternative Names
};

/**
 * @brief HTTP 응답 구조체
 */
struct HttpResponse {
    int status_code{0};
    std::string status_text;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::string effective_url;          ///< 리다이렉션 후 최종 URL

    // 성능 정보
    double total_time_seconds{0.0};
    double dns_time_seconds{0.0};
    double connect_time_seconds{0.0};
    double tls_time_seconds{0.0};
    size_t bytes_downloaded{0};
    size_t bytes_uploaded{0};

    // SSL 인증서 정보
    std::optional<CertificateInfo> certificate;

    // 에러 정보
    bool success{false};
    std::string error_message;
    int curl_error_code{0};

    /**
     * @brief 응답 성공 여부 (2xx)
     */
    [[nodiscard]] bool isOk() const {
        return success && status_code >= 200 && status_code < 300;
    }

    /**
     * @brief 리다이렉션 여부 (3xx)
     */
    [[nodiscard]] bool isRedirect() const {
        return status_code >= 300 && status_code < 400;
    }
};

/**
 * @brief 진행률 콜백
 * @param downloaded 다운로드된 바이트
 * @param total 전체 바이트 (알 수 없으면 0)
 * @return false를 반환하면 전송 중단
 */
using ProgressCallback = std::function<bool(size_t downloaded, size_t total)>;

/**
 * @brief HTTP 클라이언트
 * 
 * libcurl을 사용하여 HTTP/HTTPS 요청을 처리합니다.
 * 스레드 안전하며, 동기/비동기 요청을 모두 지원합니다.
 */
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // 복사 금지, 이동 허용
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;

    /**
     * @brief libcurl 전역 초기화 (프로세스당 한 번)
     */
    static void globalInit();

    /**
     * @brief libcurl 전역 정리
     */
    static void globalCleanup();

    // ============================
    // 동기 요청
    // ============================

    /**
     * @brief HTTP 요청 실행 (동기)
     * @param request 요청 정보
     * @param progress_cb 진행률 콜백 (선택)
     * @return HTTP 응답
     */
    [[nodiscard]] HttpResponse send(
        const HttpRequest& request,
        ProgressCallback progress_cb = nullptr
    );

    /**
     * @brief 간편 GET 요청
     */
    [[nodiscard]] HttpResponse get(const std::string& url);

    /**
     * @brief 간편 POST 요청
     */
    [[nodiscard]] HttpResponse post(
        const std::string& url,
        const std::string& body,
        const std::string& content_type = "application/json"
    );

    // ============================
    // 비동기 요청
    // ============================

    /**
     * @brief HTTP 요청 실행 (비동기)
     * @return future로 응답 수신
     */
    [[nodiscard]] std::future<HttpResponse> sendAsync(const HttpRequest& request);

    // ============================
    // 설정
    // ============================

    /**
     * @brief User-Agent 설정
     */
    void setUserAgent(const std::string& user_agent);

    /**
     * @brief 기본 헤더 추가 (모든 요청에 적용)
     */
    void setDefaultHeader(const std::string& key, const std::string& value);

    /**
     * @brief 프록시 설정
     */
    void setProxy(const std::string& proxy_url);

    /**
     * @brief SSL 검증 활성화/비활성화
     */
    void setSslVerification(bool enable);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ordinal::network
