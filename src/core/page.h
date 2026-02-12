#pragma once

/**
 * @file page.h
 * @brief 웹 페이지 데이터 모델
 * 
 * 로드된 웹 페이지의 원시 데이터, DOM 트리, 스타일 정보를 관리합니다.
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>

namespace ordinal::core {

/**
 * @brief HTTP 응답 헤더
 */
struct HttpResponseInfo {
    int status_code{0};                                     ///< HTTP 상태 코드
    std::string status_text;                                ///< 상태 텍스트
    std::unordered_map<std::string, std::string> headers;   ///< 응답 헤더
    std::string content_type;                               ///< Content-Type
    std::string charset{"utf-8"};                           ///< 문자 인코딩
    size_t content_length{0};                               ///< Content-Length
};

/**
 * @brief 페이지에 포함된 리소스
 */
struct PageResource {
    enum class Type {
        Script,         ///< JavaScript
        Stylesheet,     ///< CSS
        Image,          ///< 이미지
        Font,           ///< 폰트
        Media,          ///< 오디오/비디오
        Other           ///< 기타
    };

    Type type;
    std::string url;
    size_t size_bytes{0};
    bool loaded{false};
    bool blocked{false};    ///< 보안/프라이버시로 차단됨
    std::string block_reason;
};

/**
 * @brief 웹 페이지 클래스
 * 
 * 하나의 웹 페이지에 대한 모든 데이터를 관리합니다.
 */
class Page {
public:
    Page() = default;
    ~Page() = default;

    Page(Page&&) noexcept = default;
    Page& operator=(Page&&) noexcept = default;

    // ============================
    // 기본 속성
    // ============================

    void setUrl(const std::string& url) { url_ = url; }
    [[nodiscard]] const std::string& url() const { return url_; }

    void setTitle(const std::string& title) { title_ = title; }
    [[nodiscard]] const std::string& title() const { return title_; }

    void setFavicon(const std::string& favicon_url) { favicon_url_ = favicon_url; }
    [[nodiscard]] const std::string& favicon() const { return favicon_url_; }

    // ============================
    // 원시 데이터
    // ============================

    /**
     * @brief HTML 소스 코드 설정
     */
    void setHtmlSource(const std::string& html) { html_source_ = html; }
    [[nodiscard]] const std::string& htmlSource() const { return html_source_; }

    /**
     * @brief HTTP 응답 정보 설정
     */
    void setResponseInfo(const HttpResponseInfo& info) { response_info_ = info; }
    [[nodiscard]] const HttpResponseInfo& responseInfo() const { return response_info_; }

    // ============================
    // 리소스 관리
    // ============================

    /**
     * @brief 리소스 추가
     */
    void addResource(PageResource resource) {
        resources_.push_back(std::move(resource));
    }

    /**
     * @brief 리소스 목록 조회
     */
    [[nodiscard]] const std::vector<PageResource>& resources() const { return resources_; }

    /**
     * @brief 차단된 리소스 수 조회
     */
    [[nodiscard]] size_t blockedResourceCount() const {
        size_t count = 0;
        for (const auto& r : resources_) {
            if (r.blocked) ++count;
        }
        return count;
    }

    /**
     * @brief 전체 페이지 크기 (바이트)
     */
    [[nodiscard]] size_t totalSizeBytes() const {
        size_t total = html_source_.size();
        for (const auto& r : resources_) {
            if (r.loaded) total += r.size_bytes;
        }
        return total;
    }

    // ============================
    // 보안 메타데이터
    // ============================

    /**
     * @brief 페이지 로드 시간 설정
     */
    void setLoadTime(std::chrono::milliseconds ms) { load_time_ = ms; }
    [[nodiscard]] std::chrono::milliseconds loadTime() const { return load_time_; }

    /**
     * @brief 보안 경고 추가
     */
    void addSecurityWarning(const std::string& warning) {
        security_warnings_.push_back(warning);
    }

    [[nodiscard]] const std::vector<std::string>& securityWarnings() const {
        return security_warnings_;
    }

    [[nodiscard]] bool hasSecurityWarnings() const {
        return !security_warnings_.empty();
    }

private:
    std::string url_;
    std::string title_;
    std::string favicon_url_;
    std::string html_source_;

    HttpResponseInfo response_info_;
    std::vector<PageResource> resources_;
    std::chrono::milliseconds load_time_{0};
    std::vector<std::string> security_warnings_;
};

} // namespace ordinal::core
