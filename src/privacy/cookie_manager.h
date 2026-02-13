#pragma once

/**
 * @file cookie_manager.h
 * @brief 쿠키 관리자
 *
 * 쿠키 저장소(쿠키 jar), 퍼스트/서드파티 분류, SameSite 정책 강제,
 * 쿠키 뷰어, 선택적 삭제, 추적기 분류, 만료 관리.
 */

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <functional>

namespace ordinal::data {
class DataStore;
}

namespace ordinal::privacy {

// ============================================================
// 쿠키 구조
// ============================================================

/**
 * @brief SameSite 정책
 */
enum class SameSitePolicy {
    None,       ///< 크로스사이트 허용 (Secure 필요)
    Lax,        ///< 최상위 네비게이션에서만 전송
    Strict      ///< 동일 사이트에서만 전송
};

/**
 * @brief 쿠키 분류
 */
enum class CookieClassification {
    Functional,     ///< 기능 쿠키 (필수)
    Analytics,      ///< 분석 쿠키
    Advertising,    ///< 광고 쿠키
    Social,         ///< 소셜 미디어 쿠키
    Tracking,       ///< 추적 쿠키
    Unknown         ///< 분류 불가
};

/**
 * @brief 파티 분류
 */
enum class CookieParty {
    FirstParty,     ///< 퍼스트파티 (현재 사이트)
    ThirdParty      ///< 서드파티 (외부 사이트)
};

/**
 * @brief 쿠키 데이터
 */
struct Cookie {
    int64_t id{0};                      ///< DB ID
    std::string name;                   ///< 쿠키 이름
    std::string value;                  ///< 쿠키 값
    std::string domain;                 ///< 도메인 (.example.com)
    std::string path;                   ///< 경로
    int64_t creation_time{0};           ///< 생성 시간 (Unix epoch)
    int64_t expiry_time{0};             ///< 만료 시간 (0이면 세션)
    int64_t last_access_time{0};        ///< 마지막 접근 시간
    bool secure{false};                 ///< Secure 플래그
    bool http_only{false};              ///< HttpOnly 플래그
    SameSitePolicy same_site{SameSitePolicy::Lax};
    CookieParty party{CookieParty::FirstParty};
    CookieClassification classification{CookieClassification::Unknown};
    int64_t size_bytes{0};              ///< 크기 (바이트)
    bool is_session{false};             ///< 세션 쿠키 여부
    bool blocked{false};                ///< 차단 여부
};

/**
 * @brief 도메인별 쿠키 요약
 */
struct DomainCookieSummary {
    std::string domain;                 ///< 도메인
    int total_cookies{0};               ///< 총 쿠키 수
    int first_party{0};                 ///< 퍼스트파티 수
    int third_party{0};                 ///< 서드파티 수
    int tracking{0};                    ///< 추적 쿠키 수
    int blocked{0};                     ///< 차단된 수
    int64_t total_size{0};              ///< 총 크기 (바이트)
};

/**
 * @brief 쿠키 관리자 설정
 */
struct CookieConfig {
    bool block_third_party{false};          ///< 서드파티 쿠키 차단
    bool block_tracking_cookies{true};      ///< 추적 쿠키 차단
    bool enforce_same_site_lax{true};       ///< SameSite=Lax 기본 강제
    bool enforce_secure_for_none{true};     ///< SameSite=None 시 Secure 필수
    bool auto_delete_expired{true};         ///< 만료 쿠키 자동 삭제
    int max_cookies_per_domain{150};        ///< 도메인당 최대 쿠키 수
    int max_cookie_size{4096};              ///< 단일 쿠키 최대 크기 (바이트)
    std::vector<std::string> whitelist_domains;  ///< 허용 도메인 목록
    std::vector<std::string> blacklist_domains;  ///< 차단 도메인 목록
};

// ============================================================
// CookieManager
// ============================================================

/**
 * @brief 쿠키 관리자
 *
 * 쿠키를 SQLite에 저장하고, 퍼스트/서드파티 분류, SameSite 강제,
 * 추적기 탐지, 도메인별 관리를 수행합니다.
 */
class CookieManager {
public:
    explicit CookieManager(std::shared_ptr<ordinal::data::DataStore> store);
    ~CookieManager();

    CookieManager(const CookieManager&) = delete;
    CookieManager& operator=(const CookieManager&) = delete;

    /**
     * @brief 초기화
     */
    bool initialize(const CookieConfig& config = {});

    // ============================
    // 쿠키 저장/조회
    // ============================

    /**
     * @brief 쿠키 설정 (Set-Cookie 처리)
     * @param cookie 쿠키 데이터
     * @param page_domain 현재 페이지 도메인 (파티 판별용)
     * @return 저장 여부 (차단 시 false)
     */
    bool setCookie(Cookie& cookie, const std::string& page_domain);

    /**
     * @brief URL에 대한 쿠키 조회 (요청 시 전송할 쿠키)
     * @param url 요청 URL
     * @param page_domain 현재 페이지 도메인
     * @param is_secure HTTPS 여부
     * @return Cookie 헤더 문자열
     */
    std::string getCookieHeader(const std::string& url,
                                 const std::string& page_domain,
                                 bool is_secure = true) const;

    /**
     * @brief 도메인의 모든 쿠키 조회
     */
    std::vector<Cookie> getCookiesForDomain(const std::string& domain) const;

    /**
     * @brief 전체 쿠키 조회
     * @param limit 최대 수
     */
    std::vector<Cookie> getAllCookies(int limit = 10000) const;

    /**
     * @brief 특정 쿠키 조회
     */
    std::optional<Cookie> getCookie(int64_t cookie_id) const;

    // ============================
    // 쿠키 삭제
    // ============================

    /**
     * @brief 특정 쿠키 삭제
     */
    bool deleteCookie(int64_t cookie_id);

    /**
     * @brief 도메인의 모든 쿠키 삭제
     */
    int deleteCookiesForDomain(const std::string& domain);

    /**
     * @brief 서드파티 쿠키 일괄 삭제
     */
    int deleteThirdPartyCookies();

    /**
     * @brief 추적 쿠키 일괄 삭제
     */
    int deleteTrackingCookies();

    /**
     * @brief 만료 쿠키 정리
     */
    int deleteExpiredCookies();

    /**
     * @brief 모든 쿠키 삭제
     */
    int deleteAllCookies();

    /**
     * @brief 기간별 쿠키 삭제
     * @param since_epoch 이 시간 이후 생성된 쿠키 삭제
     */
    int deleteCookiesSince(int64_t since_epoch);

    // ============================
    // 분석/뷰어
    // ============================

    /**
     * @brief 도메인별 쿠키 요약
     */
    std::vector<DomainCookieSummary> getDomainSummaries() const;

    /**
     * @brief 총 쿠키 수
     */
    int getTotalCookieCount() const;

    /**
     * @brief 총 쿠키 크기
     */
    int64_t getTotalCookieSize() const;

    /**
     * @brief 추적기 도메인 목록
     */
    std::vector<std::string> getTrackerDomains() const;

    // ============================
    // 설정
    // ============================

    /**
     * @brief 설정 업데이트
     */
    void updateConfig(const CookieConfig& config);

    /**
     * @brief 현재 설정 조회
     */
    [[nodiscard]] const CookieConfig& config() const { return config_; }

    /**
     * @brief 도메인을 화이트리스트에 추가
     */
    void addToWhitelist(const std::string& domain);

    /**
     * @brief 도메인을 블랙리스트에 추가
     */
    void addToBlacklist(const std::string& domain);

private:
    std::shared_ptr<ordinal::data::DataStore> store_;
    CookieConfig config_;
    mutable std::mutex mutex_;

    /// 알려진 추적기 도메인 목록
    std::unordered_set<std::string> tracker_domains_;

    /**
     * @brief 추적기 도메인 목록 로드
     */
    void loadTrackerDomains();

    /**
     * @brief 쿠키 분류 판별
     */
    CookieClassification classifyCookie(const Cookie& cookie) const;

    /**
     * @brief 퍼스트/서드파티 판별
     */
    CookieParty determinParty(const std::string& cookie_domain,
                               const std::string& page_domain) const;

    /**
     * @brief 쿠키 차단 여부 확인
     */
    bool shouldBlock(const Cookie& cookie) const;

    /**
     * @brief 도메인 매칭 (서브도메인 포함)
     */
    static bool domainMatches(const std::string& cookie_domain,
                               const std::string& request_domain);

    /**
     * @brief 경로 매칭
     */
    static bool pathMatches(const std::string& cookie_path,
                             const std::string& request_path);

    /**
     * @brief URL에서 도메인 추출
     */
    static std::string extractDomain(const std::string& url);

    /**
     * @brief URL에서 경로 추출
     */
    static std::string extractPath(const std::string& url);

    /**
     * @brief 현재 Unix epoch (초)
     */
    static int64_t nowEpoch();

    /**
     * @brief DB 행 → Cookie 변환
     */
    Cookie rowToCookie(const std::unordered_map<std::string,
        std::variant<std::nullptr_t, int64_t, double, std::string,
                     std::vector<uint8_t>>>& row) const;
};

} // namespace ordinal::privacy
