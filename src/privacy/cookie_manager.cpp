/**
 * @file cookie_manager.cpp
 * @brief 쿠키 관리자 전체 구현
 *
 * 쿠키 저장소(jar) 관리, 추가/삭제/조회, 도메인 매칭, 경로 매칭,
 * 만료 검사, 퍼스트/서드파티 분류, SameSite 정책 강제,
 * HttpOnly/Secure 플래그 강제, 직렬화/역직렬화, PrivacyTracker 통합.
 */

#include "cookie_manager.h"
#include "../data/data_store.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <regex>
#include <sstream>

namespace ordinal::privacy {

using namespace ordinal::data;
using Clock = std::chrono::system_clock;

// ============================================================
// 알려진 추적기 도메인 목록 (내장)
// ============================================================

namespace {

/// 주요 추적기 도메인 (EasyPrivacy 기반 축약)
const std::vector<std::string> KNOWN_TRACKER_DOMAINS = {
    // 광고/추적 네트워크
    "doubleclick.net", "googlesyndication.com", "googleadservices.com",
    "google-analytics.com", "googletagmanager.com", "googletagservices.com",
    "facebook.net", "facebook.com", "fbcdn.net",
    "amazon-adsystem.com", "advertising.com",
    "adsrvr.org", "adnxs.com", "rubiconproject.com",
    "pubmatic.com", "openx.net", "casalemedia.com",
    "criteo.com", "criteo.net", "outbrain.com", "taboola.com",
    "scorecardresearch.com", "quantserve.com", "moatads.com",
    // 분석
    "hotjar.com", "fullstory.com", "mouseflow.com",
    "segment.io", "segment.com", "mixpanel.com",
    "amplitude.com", "heapanalytics.com",
    // 소셜 추적
    "twitter.com", "platform.twitter.com",
    "connect.facebook.net", "linkedin.com",
    // 핑거프린팅
    "fingerprintjs.com", "ipinfo.io",
    // 기타 추적기
    "bluekai.com", "exelator.com", "demdex.net",
    "krxd.net", "rlcdn.com", "tapad.com",
    "sharethrough.com", "bidswitch.net",
};

/// 도메인에서 등록 가능 도메인(eTLD+1) 추출 (간이 구현)
std::string getRegistrableDomain(const std::string& domain) {
    // 간이 eTLD+1: 마지막 두 세그먼트 추출
    // co.kr, co.uk 등의 ccTLD는 세 세그먼트
    static const std::vector<std::string> TWO_PART_TLDS = {
        "co.kr", "co.uk", "co.jp", "or.kr", "or.jp",
        "ne.jp", "ac.kr", "ac.uk", "go.kr", "go.jp",
        "com.au", "com.br", "com.cn", "com.tw", "com.hk",
        "net.au", "org.au", "org.uk",
    };

    std::string d = domain;
    // 앞의 점 제거
    if (!d.empty() && d[0] == '.') {
        d = d.substr(1);
    }

    // 점으로 분할
    std::vector<std::string> parts;
    std::istringstream iss(d);
    std::string part;
    while (std::getline(iss, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    if (parts.size() <= 2) {
        return d;
    }

    // 2단계 TLD 확인
    std::string last_two = parts[parts.size() - 2] + "." + parts.back();
    for (const auto& tld : TWO_PART_TLDS) {
        if (last_two == tld && parts.size() >= 3) {
            return parts[parts.size() - 3] + "." + last_two;
        }
    }

    // 기본: 마지막 두 세그먼트
    return last_two;
}

} // anonymous namespace

// ============================================================
// 생성자 / 소멸자
// ============================================================

CookieManager::CookieManager(std::shared_ptr<DataStore> store)
    : store_(std::move(store)) {
}

CookieManager::~CookieManager() = default;

// ============================================================
// 초기화
// ============================================================

bool CookieManager::initialize(const CookieConfig& config) {
    std::lock_guard lock(mutex_);
    config_ = config;

    if (!store_ || !store_->isOpen()) {
        std::cerr << "[CookieManager] DataStore가 열려있지 않습니다." << std::endl;
        return false;
    }

    // 쿠키 테이블 생성
    const std::string create_sql = R"SQL(
        CREATE TABLE IF NOT EXISTS cookies (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            name            TEXT NOT NULL,
            value           TEXT NOT NULL DEFAULT '',
            domain          TEXT NOT NULL,
            path            TEXT NOT NULL DEFAULT '/',
            creation_time   INTEGER NOT NULL,
            expiry_time     INTEGER NOT NULL DEFAULT 0,
            last_access     INTEGER NOT NULL DEFAULT 0,
            secure          INTEGER NOT NULL DEFAULT 0,
            http_only       INTEGER NOT NULL DEFAULT 0,
            same_site       INTEGER NOT NULL DEFAULT 1,
            party           INTEGER NOT NULL DEFAULT 0,
            classification  INTEGER NOT NULL DEFAULT 5,
            size_bytes      INTEGER NOT NULL DEFAULT 0,
            is_session      INTEGER NOT NULL DEFAULT 0,
            blocked         INTEGER NOT NULL DEFAULT 0,
            UNIQUE(name, domain, path)
        );
    )SQL";

    if (store_->execute(create_sql) < 0) {
        std::cerr << "[CookieManager] 쿠키 테이블 생성 실패: "
                  << store_->lastError() << std::endl;
        return false;
    }

    // 인덱스 생성
    store_->execute("CREATE INDEX IF NOT EXISTS idx_cookies_domain ON cookies(domain);");
    store_->execute("CREATE INDEX IF NOT EXISTS idx_cookies_expiry ON cookies(expiry_time);");
    store_->execute("CREATE INDEX IF NOT EXISTS idx_cookies_party ON cookies(party);");
    store_->execute("CREATE INDEX IF NOT EXISTS idx_cookies_classification ON cookies(classification);");

    // 추적기 도메인 목록 로드
    loadTrackerDomains();

    // 만료 쿠키 자동 정리
    if (config_.auto_delete_expired) {
        deleteExpiredCookies();
    }

    std::cout << "[CookieManager] 초기화 완료 (추적기 도메인: "
              << tracker_domains_.size() << "개)" << std::endl;
    return true;
}

// ============================================================
// 쿠키 설정 (Set-Cookie 처리)
// ============================================================

bool CookieManager::setCookie(Cookie& cookie, const std::string& page_domain) {
    std::lock_guard lock(mutex_);

    // 현재 시간 설정
    int64_t now = nowEpoch();
    if (cookie.creation_time == 0) {
        cookie.creation_time = now;
    }
    cookie.last_access_time = now;

    // 세션 쿠키 판별
    cookie.is_session = (cookie.expiry_time == 0);

    // 크기 계산
    cookie.size_bytes = static_cast<int64_t>(
        cookie.name.size() + cookie.value.size());

    // 크기 제한 확인
    if (cookie.size_bytes > config_.max_cookie_size) {
        std::cerr << "[CookieManager] 쿠키 크기 초과: " << cookie.name
                  << " (" << cookie.size_bytes << " > "
                  << config_.max_cookie_size << ")" << std::endl;
        return false;
    }

    // 도메인 정규화 (앞에 점 추가)
    if (!cookie.domain.empty() && cookie.domain[0] != '.') {
        cookie.domain = "." + cookie.domain;
    }

    // 경로 기본값
    if (cookie.path.empty()) {
        cookie.path = "/";
    }

    // 퍼스트/서드파티 분류
    cookie.party = determinParty(cookie.domain, page_domain);

    // 쿠키 분류 (추적기 등)
    cookie.classification = classifyCookie(cookie);

    // SameSite=None인 경우 Secure 플래그 필수 (RFC 6265bis)
    if (config_.enforce_secure_for_none &&
        cookie.same_site == SameSitePolicy::None &&
        !cookie.secure) {
        std::cerr << "[CookieManager] SameSite=None 쿠키에 Secure 필수: "
                  << cookie.name << std::endl;
        cookie.blocked = true;
        return false;
    }

    // SameSite=Lax 기본 강제 (명시 없으면 Lax)
    if (config_.enforce_same_site_lax &&
        cookie.same_site == SameSitePolicy::None &&
        !cookie.secure) {
        cookie.same_site = SameSitePolicy::Lax;
    }

    // 차단 여부 확인
    if (shouldBlock(cookie)) {
        cookie.blocked = true;
        std::cout << "[CookieManager] 쿠키 차단: " << cookie.name
                  << " (도메인: " << cookie.domain << ")" << std::endl;
        return false;
    }

    // 도메인당 쿠키 수 제한 확인
    auto existing = getCookiesForDomain(cookie.domain);
    if (static_cast<int>(existing.size()) >= config_.max_cookies_per_domain) {
        // 가장 오래된 쿠키 삭제 (LRU)
        auto oldest = std::min_element(existing.begin(), existing.end(),
            [](const Cookie& a, const Cookie& b) {
                return a.last_access_time < b.last_access_time;
            });
        if (oldest != existing.end()) {
            store_->execute("DELETE FROM cookies WHERE id = ?;",
                {static_cast<int64_t>(oldest->id)});
        }
    }

    // UPSERT (같은 name+domain+path 덮어쓰기)
    const std::string upsert_sql = R"SQL(
        INSERT INTO cookies (
            name, value, domain, path, creation_time, expiry_time,
            last_access, secure, http_only, same_site, party,
            classification, size_bytes, is_session, blocked
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(name, domain, path) DO UPDATE SET
            value = excluded.value,
            expiry_time = excluded.expiry_time,
            last_access = excluded.last_access,
            secure = excluded.secure,
            http_only = excluded.http_only,
            same_site = excluded.same_site,
            party = excluded.party,
            classification = excluded.classification,
            size_bytes = excluded.size_bytes,
            is_session = excluded.is_session,
            blocked = excluded.blocked;
    )SQL";

    int result = store_->execute(upsert_sql, {
        cookie.name, cookie.value, cookie.domain, cookie.path,
        cookie.creation_time, cookie.expiry_time, cookie.last_access_time,
        static_cast<int64_t>(cookie.secure ? 1 : 0),
        static_cast<int64_t>(cookie.http_only ? 1 : 0),
        static_cast<int64_t>(static_cast<int>(cookie.same_site)),
        static_cast<int64_t>(static_cast<int>(cookie.party)),
        static_cast<int64_t>(static_cast<int>(cookie.classification)),
        cookie.size_bytes,
        static_cast<int64_t>(cookie.is_session ? 1 : 0),
        static_cast<int64_t>(cookie.blocked ? 1 : 0),
    });

    if (result < 0) {
        std::cerr << "[CookieManager] 쿠키 저장 실패: "
                  << store_->lastError() << std::endl;
        return false;
    }

    cookie.id = store_->lastInsertRowId();
    return true;
}

// ============================================================
// 쿠키 조회 (요청용 Cookie 헤더 생성)
// ============================================================

std::string CookieManager::getCookieHeader(
    const std::string& url,
    const std::string& page_domain,
    bool is_secure) const {

    std::lock_guard lock(mutex_);

    std::string request_domain = extractDomain(url);
    std::string request_path = extractPath(url);
    int64_t now = nowEpoch();

    // 해당 도메인에 매칭되는 모든 쿠키 조회
    auto rows = store_->query(
        "SELECT * FROM cookies WHERE blocked = 0;");

    std::vector<Cookie> matched;

    for (const auto& row : rows) {
        Cookie c = rowToCookie(row);

        // 만료 확인 (세션 쿠키 제외)
        if (!c.is_session && c.expiry_time > 0 && c.expiry_time < now) {
            continue;
        }

        // 도메인 매칭
        if (!domainMatches(c.domain, request_domain)) {
            continue;
        }

        // 경로 매칭
        if (!pathMatches(c.path, request_path)) {
            continue;
        }

        // Secure 플래그 확인 — HTTPS에서만 전송
        if (c.secure && !is_secure) {
            continue;
        }

        // SameSite 정책 확인
        bool is_same_site = (getRegistrableDomain(c.domain) ==
                             getRegistrableDomain(page_domain));

        if (c.same_site == SameSitePolicy::Strict && !is_same_site) {
            continue; // Strict: 동일 사이트에서만 전송
        }

        if (c.same_site == SameSitePolicy::Lax && !is_same_site) {
            // Lax: 최상위 네비게이션에서만 전송 (GET 요청)
            // 여기서는 간략화 — 서드파티 서브리소스에서 차단
            continue;
        }

        // SameSite=None은 Secure 필수 (이미 위에서 확인)

        matched.push_back(c);
    }

    // 경로 길이 내림차순 정렬 (RFC 6265 §5.4)
    std::sort(matched.begin(), matched.end(),
        [](const Cookie& a, const Cookie& b) {
            if (a.path.size() != b.path.size()) {
                return a.path.size() > b.path.size();
            }
            return a.creation_time < b.creation_time;
        });

    // Cookie 헤더 문자열 생성
    std::ostringstream oss;
    for (size_t i = 0; i < matched.size(); ++i) {
        if (i > 0) oss << "; ";
        oss << matched[i].name << "=" << matched[i].value;

        // last_access 업데이트
        store_->execute(
            "UPDATE cookies SET last_access = ? WHERE id = ?;",
            {now, matched[i].id});
    }

    return oss.str();
}

// ============================================================
// 도메인별 쿠키 조회
// ============================================================

std::vector<Cookie> CookieManager::getCookiesForDomain(
    const std::string& domain) const {

    // 점 접두사 정규화
    std::string d = domain;
    if (!d.empty() && d[0] != '.') {
        d = "." + d;
    }

    auto rows = store_->query(
        "SELECT * FROM cookies WHERE domain = ? OR domain = ? ORDER BY creation_time DESC;",
        {d, domain});

    std::vector<Cookie> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.push_back(rowToCookie(row));
    }
    return result;
}

// ============================================================
// 전체 쿠키 조회
// ============================================================

std::vector<Cookie> CookieManager::getAllCookies(int limit) const {
    std::lock_guard lock(mutex_);

    auto rows = store_->query(
        "SELECT * FROM cookies ORDER BY last_access DESC LIMIT ?;",
        {static_cast<int64_t>(limit)});

    std::vector<Cookie> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.push_back(rowToCookie(row));
    }
    return result;
}

// ============================================================
// 특정 쿠키 조회
// ============================================================

std::optional<Cookie> CookieManager::getCookie(int64_t cookie_id) const {
    std::lock_guard lock(mutex_);

    auto rows = store_->query(
        "SELECT * FROM cookies WHERE id = ?;",
        {cookie_id});

    if (rows.empty()) {
        return std::nullopt;
    }
    return rowToCookie(rows[0]);
}

// ============================================================
// 쿠키 삭제
// ============================================================

bool CookieManager::deleteCookie(int64_t cookie_id) {
    std::lock_guard lock(mutex_);
    return store_->execute(
        "DELETE FROM cookies WHERE id = ?;",
        {cookie_id}) > 0;
}

int CookieManager::deleteCookiesForDomain(const std::string& domain) {
    std::lock_guard lock(mutex_);

    std::string d = domain;
    if (!d.empty() && d[0] != '.') {
        d = "." + d;
    }

    int count = store_->execute(
        "DELETE FROM cookies WHERE domain = ? OR domain = ?;",
        {d, domain});

    std::cout << "[CookieManager] 도메인 " << domain
              << "의 쿠키 " << count << "개 삭제" << std::endl;
    return count;
}

int CookieManager::deleteThirdPartyCookies() {
    std::lock_guard lock(mutex_);

    int count = store_->execute(
        "DELETE FROM cookies WHERE party = ?;",
        {static_cast<int64_t>(static_cast<int>(CookieParty::ThirdParty))});

    std::cout << "[CookieManager] 서드파티 쿠키 " << count << "개 삭제" << std::endl;
    return count;
}

int CookieManager::deleteTrackingCookies() {
    std::lock_guard lock(mutex_);

    int count = store_->execute(
        "DELETE FROM cookies WHERE classification = ?;",
        {static_cast<int64_t>(static_cast<int>(CookieClassification::Tracking))});

    std::cout << "[CookieManager] 추적 쿠키 " << count << "개 삭제" << std::endl;
    return count;
}

int CookieManager::deleteExpiredCookies() {
    int64_t now = nowEpoch();

    int count = store_->execute(
        "DELETE FROM cookies WHERE is_session = 0 AND expiry_time > 0 AND expiry_time < ?;",
        {now});

    if (count > 0) {
        std::cout << "[CookieManager] 만료 쿠키 " << count << "개 정리" << std::endl;
    }
    return count;
}

int CookieManager::deleteAllCookies() {
    std::lock_guard lock(mutex_);

    int count = store_->execute("DELETE FROM cookies;");
    std::cout << "[CookieManager] 모든 쿠키 삭제 (" << count << "개)" << std::endl;
    return count;
}

int CookieManager::deleteCookiesSince(int64_t since_epoch) {
    std::lock_guard lock(mutex_);

    int count = store_->execute(
        "DELETE FROM cookies WHERE creation_time >= ?;",
        {since_epoch});

    std::cout << "[CookieManager] " << since_epoch
              << " 이후 쿠키 " << count << "개 삭제" << std::endl;
    return count;
}

// ============================================================
// 분석/뷰어
// ============================================================

std::vector<DomainCookieSummary> CookieManager::getDomainSummaries() const {
    std::lock_guard lock(mutex_);

    // 도메인별 그룹 집계
    auto rows = store_->query(R"SQL(
        SELECT
            domain,
            COUNT(*) as total,
            SUM(CASE WHEN party = 0 THEN 1 ELSE 0 END) as first_party,
            SUM(CASE WHEN party = 1 THEN 1 ELSE 0 END) as third_party,
            SUM(CASE WHEN classification = 4 THEN 1 ELSE 0 END) as tracking,
            SUM(CASE WHEN blocked = 1 THEN 1 ELSE 0 END) as blocked_count,
            SUM(size_bytes) as total_size
        FROM cookies
        GROUP BY domain
        ORDER BY total DESC;
    )SQL");

    std::vector<DomainCookieSummary> summaries;
    summaries.reserve(rows.size());

    for (const auto& row : rows) {
        DomainCookieSummary s;
        // 값 추출 헬퍼
        auto getStr = [&](const std::string& key) -> std::string {
            auto it = row.find(key);
            if (it != row.end()) {
                if (auto* v = std::get_if<std::string>(&it->second)) return *v;
            }
            return {};
        };
        auto getInt = [&](const std::string& key) -> int64_t {
            auto it = row.find(key);
            if (it != row.end()) {
                if (auto* v = std::get_if<int64_t>(&it->second)) return *v;
            }
            return 0;
        };

        s.domain = getStr("domain");
        s.total_cookies = static_cast<int>(getInt("total"));
        s.first_party = static_cast<int>(getInt("first_party"));
        s.third_party = static_cast<int>(getInt("third_party"));
        s.tracking = static_cast<int>(getInt("tracking"));
        s.blocked = static_cast<int>(getInt("blocked_count"));
        s.total_size = getInt("total_size");
        summaries.push_back(s);
    }

    return summaries;
}

int CookieManager::getTotalCookieCount() const {
    std::lock_guard lock(mutex_);

    auto val = store_->queryScalar("SELECT COUNT(*) FROM cookies;");
    if (val && std::holds_alternative<int64_t>(*val)) {
        return static_cast<int>(std::get<int64_t>(*val));
    }
    return 0;
}

int64_t CookieManager::getTotalCookieSize() const {
    std::lock_guard lock(mutex_);

    auto val = store_->queryScalar("SELECT SUM(size_bytes) FROM cookies;");
    if (val && std::holds_alternative<int64_t>(*val)) {
        return std::get<int64_t>(*val);
    }
    return 0;
}

std::vector<std::string> CookieManager::getTrackerDomains() const {
    std::lock_guard lock(mutex_);
    return {tracker_domains_.begin(), tracker_domains_.end()};
}

// ============================================================
// 설정
// ============================================================

void CookieManager::updateConfig(const CookieConfig& config) {
    std::lock_guard lock(mutex_);
    config_ = config;
}

void CookieManager::addToWhitelist(const std::string& domain) {
    std::lock_guard lock(mutex_);
    config_.whitelist_domains.push_back(domain);
}

void CookieManager::addToBlacklist(const std::string& domain) {
    std::lock_guard lock(mutex_);
    config_.blacklist_domains.push_back(domain);
}

// ============================================================
// Private: 추적기 도메인 로드
// ============================================================

void CookieManager::loadTrackerDomains() {
    // 내장 추적기 도메인 로드
    for (const auto& domain : KNOWN_TRACKER_DOMAINS) {
        tracker_domains_.insert(domain);
        tracker_domains_.insert("." + domain);
    }
}

// ============================================================
// Private: 쿠키 분류 판별
// ============================================================

CookieClassification CookieManager::classifyCookie(const Cookie& cookie) const {
    std::string domain = cookie.domain;
    if (!domain.empty() && domain[0] == '.') {
        domain = domain.substr(1);
    }

    // 추적기 도메인 확인
    std::string registrable = getRegistrableDomain(domain);
    if (tracker_domains_.count(registrable) ||
        tracker_domains_.count(domain) ||
        tracker_domains_.count("." + registrable)) {
        return CookieClassification::Tracking;
    }

    // 쿠키 이름 기반 분류 (휴리스틱)
    const std::string& name = cookie.name;

    // 광고 관련
    static const std::vector<std::string> AD_PATTERNS = {
        "_ga", "_gid", "_gat", "_fbp", "_fbc", "IDE", "DSID",
        "NID", "MUID", "_uetsid", "_uetvid", "fr",
    };
    for (const auto& p : AD_PATTERNS) {
        if (name == p || name.find("ad") == 0 || name.find("Ad") == 0) {
            return CookieClassification::Advertising;
        }
    }

    // 분석 관련
    static const std::vector<std::string> ANALYTICS_PATTERNS = {
        "_ga", "_gid", "_gat", "amplitude", "mp_", "ajs_",
        "_hjid", "_hjSession", "optimizely",
    };
    for (const auto& p : ANALYTICS_PATTERNS) {
        if (name.find(p) != std::string::npos) {
            return CookieClassification::Analytics;
        }
    }

    // 소셜 미디어
    static const std::vector<std::string> SOCIAL_DOMAINS = {
        "facebook.com", "twitter.com", "linkedin.com",
        "instagram.com", "tiktok.com",
    };
    for (const auto& sd : SOCIAL_DOMAINS) {
        if (domain.find(sd) != std::string::npos) {
            return CookieClassification::Social;
        }
    }

    // 기능 쿠키 (세션, CSRF 토큰 등)
    static const std::vector<std::string> FUNCTIONAL_PATTERNS = {
        "session", "csrf", "token", "auth", "login", "lang", "locale",
        "preference", "consent", "cookie_notice",
    };
    for (const auto& p : FUNCTIONAL_PATTERNS) {
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(),
                       lower_name.begin(), ::tolower);
        if (lower_name.find(p) != std::string::npos) {
            return CookieClassification::Functional;
        }
    }

    return CookieClassification::Unknown;
}

// ============================================================
// Private: 퍼스트/서드파티 판별
// ============================================================

CookieParty CookieManager::determinParty(
    const std::string& cookie_domain,
    const std::string& page_domain) const {

    if (page_domain.empty()) {
        return CookieParty::FirstParty;
    }

    // 등록 가능 도메인(eTLD+1) 비교
    std::string cookie_reg = getRegistrableDomain(cookie_domain);
    std::string page_reg = getRegistrableDomain(page_domain);

    if (cookie_reg == page_reg) {
        return CookieParty::FirstParty;
    }

    return CookieParty::ThirdParty;
}

// ============================================================
// Private: 차단 여부 확인
// ============================================================

bool CookieManager::shouldBlock(const Cookie& cookie) const {
    std::string domain = cookie.domain;
    if (!domain.empty() && domain[0] == '.') {
        domain = domain.substr(1);
    }

    // 화이트리스트 확인 (우선)
    for (const auto& wd : config_.whitelist_domains) {
        if (domain == wd || domain.ends_with("." + wd)) {
            return false;
        }
    }

    // 블랙리스트 확인
    for (const auto& bd : config_.blacklist_domains) {
        if (domain == bd || domain.ends_with("." + bd)) {
            return true;
        }
    }

    // 서드파티 쿠키 차단 설정
    if (config_.block_third_party &&
        cookie.party == CookieParty::ThirdParty) {
        return true;
    }

    // 추적 쿠키 차단 설정
    if (config_.block_tracking_cookies &&
        cookie.classification == CookieClassification::Tracking) {
        return true;
    }

    return false;
}

// ============================================================
// Private: 도메인 매칭
// ============================================================

bool CookieManager::domainMatches(
    const std::string& cookie_domain,
    const std::string& request_domain) {

    // 정확히 일치
    if (cookie_domain == request_domain) {
        return true;
    }

    // 쿠키 도메인이 점으로 시작 — 서브도메인 매칭
    std::string cd = cookie_domain;
    if (!cd.empty() && cd[0] == '.') {
        cd = cd.substr(1);
    }

    if (request_domain == cd) {
        return true;
    }

    // request_domain이 cookie_domain의 서브도메인인지 확인
    // 예: request=sub.example.com, cookie=.example.com
    if (request_domain.size() > cd.size() &&
        request_domain.ends_with(cd) &&
        request_domain[request_domain.size() - cd.size() - 1] == '.') {
        return true;
    }

    return false;
}

// ============================================================
// Private: 경로 매칭
// ============================================================

bool CookieManager::pathMatches(
    const std::string& cookie_path,
    const std::string& request_path) {

    // RFC 6265 §5.1.4: 경로 매칭
    if (cookie_path == request_path) {
        return true;
    }

    // cookie_path가 request_path의 접두사인지 확인
    if (request_path.starts_with(cookie_path)) {
        // cookie_path가 /로 끝나거나, request_path의 다음 문자가 /
        if (cookie_path.back() == '/') {
            return true;
        }
        if (request_path.size() > cookie_path.size() &&
            request_path[cookie_path.size()] == '/') {
            return true;
        }
    }

    return false;
}

// ============================================================
// Private: URL 파싱 유틸리티
// ============================================================

std::string CookieManager::extractDomain(const std::string& url) {
    std::string u = url;

    // scheme 제거
    auto pos = u.find("://");
    if (pos != std::string::npos) {
        u = u.substr(pos + 3);
    }

    // 포트/경로 제거
    pos = u.find(':');
    if (pos != std::string::npos) u = u.substr(0, pos);
    pos = u.find('/');
    if (pos != std::string::npos) u = u.substr(0, pos);
    pos = u.find('?');
    if (pos != std::string::npos) u = u.substr(0, pos);

    // 소문자 변환
    std::transform(u.begin(), u.end(), u.begin(), ::tolower);
    return u;
}

std::string CookieManager::extractPath(const std::string& url) {
    std::string u = url;

    // scheme 제거
    auto pos = u.find("://");
    if (pos != std::string::npos) {
        u = u.substr(pos + 3);
    }

    // 호스트 이후 경로 추출
    pos = u.find('/');
    if (pos == std::string::npos) {
        return "/";
    }

    std::string path = u.substr(pos);

    // 쿼리/해시 제거
    pos = path.find('?');
    if (pos != std::string::npos) path = path.substr(0, pos);
    pos = path.find('#');
    if (pos != std::string::npos) path = path.substr(0, pos);

    return path.empty() ? "/" : path;
}

// ============================================================
// Private: 시간 유틸리티
// ============================================================

int64_t CookieManager::nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        Clock::now().time_since_epoch()).count();
}

// ============================================================
// Private: DB 행 → Cookie 변환
// ============================================================

Cookie CookieManager::rowToCookie(const DbRow& row) const {
    Cookie c;

    auto getInt = [&](const std::string& key) -> int64_t {
        auto it = row.find(key);
        if (it != row.end()) {
            if (auto* v = std::get_if<int64_t>(&it->second)) return *v;
        }
        return 0;
    };
    auto getStr = [&](const std::string& key) -> std::string {
        auto it = row.find(key);
        if (it != row.end()) {
            if (auto* v = std::get_if<std::string>(&it->second)) return *v;
        }
        return {};
    };

    c.id = getInt("id");
    c.name = getStr("name");
    c.value = getStr("value");
    c.domain = getStr("domain");
    c.path = getStr("path");
    c.creation_time = getInt("creation_time");
    c.expiry_time = getInt("expiry_time");
    c.last_access_time = getInt("last_access");
    c.secure = (getInt("secure") != 0);
    c.http_only = (getInt("http_only") != 0);
    c.same_site = static_cast<SameSitePolicy>(getInt("same_site"));
    c.party = static_cast<CookieParty>(getInt("party"));
    c.classification = static_cast<CookieClassification>(getInt("classification"));
    c.size_bytes = getInt("size_bytes");
    c.is_session = (getInt("is_session") != 0);
    c.blocked = (getInt("blocked") != 0);

    return c;
}

} // namespace ordinal::privacy
