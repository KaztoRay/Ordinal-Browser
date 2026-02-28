/**
 * @file adblock_engine.cpp
 * @brief 광고 차단 엔진 전체 구현
 *
 * 필터 리스트 로드/파싱, 네트워크 요청 매칭 (정규식 + 도메인 인덱스),
 * CSS 코스메틱 필터 통합, 구독 관리, 차단 통계 추적.
 */

#include "adblock_engine.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace ordinal::adblock {

using Clock = std::chrono::system_clock;

// ============================================================
// 내장 필터 룰 (최소 광고 차단 패턴)
// ============================================================

namespace {

/// 내장 광고 차단 필터 (EasyList 핵심 룰 축약)
const char* BUILTIN_FILTERS = R"FILTERS(
! Title: OrdinalV8 내장 필터
! Homepage: https://github.com/KaztoRay/Ordinal-Browser
! Version: 1.0

! === 주요 광고 네트워크 ===
||doubleclick.net^
||googlesyndication.com^
||googleadservices.com^
||google-analytics.com/collect$image,script
||adnxs.com^
||adsrvr.org^
||rubiconproject.com^
||pubmatic.com^
||openx.net^
||casalemedia.com^
||criteo.com^
||criteo.net^
||outbrain.com^$third-party
||taboola.com^$third-party
||amazon-adsystem.com^
||moatads.com^
||advertising.com^
||bidswitch.net^
||sharethrough.com^

! === 추적기 ===
||scorecardresearch.com^
||quantserve.com^
||bluekai.com^
||exelator.com^
||demdex.net^
||krxd.net^
||tapad.com^

! === 코스메틱 필터 ===
##.ad-banner
##.ad-container
##.ad-wrapper
##.ad-unit
##.ad-block
##.advertisement
##.ad-slot
##[id^="google_ads"]
##[id^="div-gpt-ad"]
##[class*="ad-placement"]
##[class*="sponsored"]
##.adsbygoogle
##ins.adsbygoogle
##.ad-leaderboard
##.ad-sidebar
##.ad-footer
##.native-ad
##.promoted-content
##.sponsored-content
)FILTERS";

/// 간이 eTLD+1 추출
std::string simpleRegistrableDomain(const std::string& domain) {
    std::string d = domain;
    if (!d.empty() && d[0] == '.') d = d.substr(1);

    static const std::vector<std::string> TWO_PART_TLDS = {
        "co.kr", "co.uk", "co.jp", "or.kr", "com.au", "com.br",
        "com.cn", "com.tw", "net.au", "org.uk", "ac.kr", "go.kr",
    };

    std::vector<std::string> parts;
    std::istringstream iss(d);
    std::string part;
    while (std::getline(iss, part, '.')) {
        if (!part.empty()) parts.push_back(part);
    }

    if (parts.size() <= 2) return d;

    std::string last_two = parts[parts.size() - 2] + "." + parts.back();
    for (const auto& tld : TWO_PART_TLDS) {
        if (last_two == tld && parts.size() >= 3) {
            return parts[parts.size() - 3] + "." + last_two;
        }
    }

    return last_two;
}

/// 소문자 변환
std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

} // anonymous namespace

// ============================================================
// 생성자 / 소멸자
// ============================================================

AdBlockEngine::AdBlockEngine() = default;
AdBlockEngine::~AdBlockEngine() = default;

// ============================================================
// 초기화
// ============================================================

bool AdBlockEngine::initialize(const std::string& data_dir) {
    data_dir_ = data_dir;

    // 데이터 디렉토리 생성
    std::filesystem::create_directories(data_dir);

    // 내장 필터 로드
    loadBuiltinRules();

    std::cout << "[AdBlockEngine] 초기화 완료 — 네트워크 룰: "
              << network_rules_.size() << "개, 예외 룰: "
              << exception_rules_.size() << "개, 코스메틱 룰: "
              << cosmetic_filter_.totalRuleCount() << "개" << std::endl;

    return true;
}

void AdBlockEngine::setEnabled(bool enabled) {
    enabled_ = enabled;
}

// ============================================================
// 네트워크 요청 차단 확인
// ============================================================

bool AdBlockEngine::shouldBlock(
    const std::string& url,
    const std::string& page_url,
    const std::string& resource_type) const {

    return checkRequest(url, page_url, resource_type).blocked;
}

BlockResult AdBlockEngine::checkRequest(
    const std::string& url,
    const std::string& page_url,
    const std::string& resource_type) const {

    BlockResult result;

    if (!enabled_) {
        return result;
    }

    stats_.total_checked.fetch_add(1, std::memory_order_relaxed);

    // URL 소문자화
    std::string url_lower = toLower(url);
    std::string page_domain = extractDomain(page_url);
    std::string request_domain = extractDomain(url);

    // 리소스 타입 변환
    ResourceType res_type = resource_type.empty() ?
        ResourceType::All : resourceTypeFromString(resource_type);

    // 서드파티 여부
    bool third_party = isThirdParty(url, page_url);

    std::lock_guard lock(rules_mutex_);

    // 1단계: 예외 룰 확인 (@@)
    for (const auto& rule : exception_rules_) {
        if (ruleMatches(rule, url_lower, page_domain, res_type, third_party)) {
            result.blocked = false;
            result.exception = true;
            result.matched_rule = rule.raw_text;
            result.filter_list = rule.source_list;
            return result;
        }
    }

    // 2단계: 차단 룰 확인
    for (const auto& rule : network_rules_) {
        if (ruleMatches(rule, url_lower, page_domain, res_type, third_party)) {
            result.blocked = true;
            result.matched_rule = rule.raw_text;
            result.filter_list = rule.source_list;

            // 통계 기록
            stats_.total_blocked.fetch_add(1, std::memory_order_relaxed);

            {
                std::lock_guard dl(stats_.domain_mutex);
                stats_.blocked_per_domain[request_domain]++;
            }

            if (!page_url.empty()) {
                std::lock_guard pl(stats_.page_mutex);
                stats_.blocked_per_page[page_url]++;
            }

            return result;
        }
    }

    return result;
}

// ============================================================
// CSS 코스메틱 필터
// ============================================================

std::string AdBlockEngine::getCosmeticStylesheet(const std::string& domain) const {
    if (!enabled_) return {};
    return cosmetic_filter_.generateStylesheet(domain);
}

std::vector<std::string> AdBlockEngine::getCosmeticSelectors(
    const std::string& domain) const {
    if (!enabled_) return {};
    return cosmetic_filter_.getSelectorsForDomain(domain);
}

// ============================================================
// 필터 리스트 로드
// ============================================================

int AdBlockEngine::loadFilterFile(const std::string& file_path,
                                   const std::string& name) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "[AdBlockEngine] 필터 파일 열기 실패: " << file_path << std::endl;
        return 0;
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    return loadFilterText(oss.str(), name.empty() ? file_path : name);
}

int AdBlockEngine::loadFilterText(const std::string& text,
                                   const std::string& name) {
    // 네트워크 룰 파싱
    auto new_rules = parser_.parseFilterList(text, name);

    // 코스메틱 룰 파싱
    int cosmetic_count = cosmetic_filter_.parseFilterList(text);

    {
        std::lock_guard lock(rules_mutex_);

        for (auto& rule : new_rules) {
            if (rule.is_exception) {
                exception_rules_.push_back(std::move(rule));
            } else {
                network_rules_.push_back(std::move(rule));
            }
        }

        // 인덱스 재구축
        rebuildIndices();
    }

    int total = static_cast<int>(new_rules.size()) + cosmetic_count;
    std::cout << "[AdBlockEngine] 필터 로드: " << name
              << " — 네트워크: " << new_rules.size()
              << ", 코스메틱: " << cosmetic_count << std::endl;

    return total;
}

// ============================================================
// 구독 관리
// ============================================================

void AdBlockEngine::addSubscription(const std::string& url,
                                     const std::string& title) {
    std::lock_guard lock(subscriptions_mutex_);

    // 중복 확인
    for (const auto& sub : subscriptions_) {
        if (sub.url == url) return;
    }

    FilterSubscription sub;
    sub.url = url;
    sub.title = title.empty() ? url : title;
    sub.enabled = true;
    sub.local_path = data_dir_ + "/filter_" +
        std::to_string(std::hash<std::string>{}(url)) + ".txt";

    subscriptions_.push_back(sub);
    std::cout << "[AdBlockEngine] 구독 추가: " << sub.title << std::endl;
}

void AdBlockEngine::removeSubscription(const std::string& url) {
    std::lock_guard lock(subscriptions_mutex_);

    subscriptions_.erase(
        std::remove_if(subscriptions_.begin(), subscriptions_.end(),
            [&](const FilterSubscription& s) { return s.url == url; }),
        subscriptions_.end());
}

void AdBlockEngine::setSubscriptionEnabled(const std::string& url, bool enabled) {
    std::lock_guard lock(subscriptions_mutex_);

    for (auto& sub : subscriptions_) {
        if (sub.url == url) {
            sub.enabled = enabled;
            break;
        }
    }
}

std::vector<FilterSubscription> AdBlockEngine::getSubscriptions() const {
    std::lock_guard lock(subscriptions_mutex_);
    return subscriptions_;
}

int AdBlockEngine::updateAllSubscriptions() {
    if (!download_callback_) {
        std::cerr << "[AdBlockEngine] 다운로드 콜백이 설정되지 않았습니다." << std::endl;
        return 0;
    }

    int updated = 0;
    std::vector<FilterSubscription> subs;
    {
        std::lock_guard lock(subscriptions_mutex_);
        subs = subscriptions_;
    }

    for (auto& sub : subs) {
        if (!sub.enabled) continue;

        try {
            std::string content = download_callback_(sub.url);
            if (content.empty()) continue;

            // 로컬 캐시에 저장
            std::ofstream file(sub.local_path);
            if (file.is_open()) {
                file << content;
                file.close();
            }

            // 메타데이터 추출
            auto meta = parser_.parseMetadata(content);
            sub.title = meta.title.empty() ? sub.title : meta.title;
            sub.update_interval_hours = meta.expires_hours;

            // 필터 로드
            int count = loadFilterText(content, sub.title);
            sub.rule_count = count;
            sub.last_updated = std::chrono::duration_cast<std::chrono::seconds>(
                Clock::now().time_since_epoch()).count();

            updated++;
            std::cout << "[AdBlockEngine] 구독 업데이트: " << sub.title
                      << " (" << count << "개 룰)" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "[AdBlockEngine] 구독 업데이트 실패 (" << sub.url
                      << "): " << e.what() << std::endl;
        }
    }

    // 구독 정보 업데이트
    {
        std::lock_guard lock(subscriptions_mutex_);
        for (size_t i = 0; i < subscriptions_.size() && i < subs.size(); ++i) {
            if (subscriptions_[i].url == subs[i].url) {
                subscriptions_[i] = subs[i];
            }
        }
    }

    return updated;
}

void AdBlockEngine::setDownloadCallback(DownloadCallback callback) {
    download_callback_ = std::move(callback);
}

// ============================================================
// 통계
// ============================================================

uint64_t AdBlockEngine::totalBlocked() const {
    return stats_.total_blocked.load(std::memory_order_relaxed);
}

std::unordered_map<std::string, uint64_t> AdBlockEngine::blockedPerDomain() const {
    std::lock_guard lock(stats_.domain_mutex);
    return stats_.blocked_per_domain;
}

std::unordered_map<std::string, uint64_t> AdBlockEngine::blockedPerPage() const {
    std::lock_guard lock(stats_.page_mutex);
    return stats_.blocked_per_page;
}

int AdBlockEngine::totalNetworkRuleCount() const {
    std::lock_guard lock(rules_mutex_);
    return static_cast<int>(network_rules_.size() + exception_rules_.size());
}

int AdBlockEngine::totalCosmeticRuleCount() const {
    return cosmetic_filter_.totalRuleCount();
}

void AdBlockEngine::resetStats() {
    stats_.total_blocked.store(0, std::memory_order_relaxed);
    stats_.total_checked.store(0, std::memory_order_relaxed);
    {
        std::lock_guard lock(stats_.domain_mutex);
        stats_.blocked_per_domain.clear();
    }
    {
        std::lock_guard lock(stats_.page_mutex);
        stats_.blocked_per_page.clear();
    }
}

void AdBlockEngine::recordPageBlock(const std::string& page_url) const {
    if (page_url.empty()) return;
    std::lock_guard lock(stats_.page_mutex);
    stats_.blocked_per_page[page_url]++;
}

// ============================================================
// Private: URL/도메인 유틸리티
// ============================================================

std::string AdBlockEngine::extractDomain(const std::string& url) {
    std::string u = url;
    auto pos = u.find("://");
    if (pos != std::string::npos) u = u.substr(pos + 3);
    pos = u.find('/');
    if (pos != std::string::npos) u = u.substr(0, pos);
    pos = u.find(':');
    if (pos != std::string::npos) u = u.substr(0, pos);
    std::transform(u.begin(), u.end(), u.begin(), ::tolower);
    return u;
}

bool AdBlockEngine::isThirdParty(const std::string& request_url,
                                  const std::string& page_url) {
    if (page_url.empty()) return false;

    std::string req_domain = getRegistrableDomain(extractDomain(request_url));
    std::string page_domain = getRegistrableDomain(extractDomain(page_url));

    return req_domain != page_domain;
}

std::string AdBlockEngine::getRegistrableDomain(const std::string& domain) {
    return simpleRegistrableDomain(domain);
}

// ============================================================
// Private: 룰 매칭
// ============================================================

bool AdBlockEngine::ruleMatches(
    const NetworkRule& rule,
    const std::string& url,
    const std::string& page_domain,
    ResourceType res_type,
    bool is_third_party) {

    // 리소스 타입 확인
    if (rule.resource_types != ResourceType::All) {
        if ((rule.resource_types & res_type) == ResourceType::None) {
            return false;
        }
    }

    // 서드파티 옵션 확인
    if (rule.third_party && !is_third_party) return false;
    if (rule.first_party && is_third_party) return false;

    // 도메인 제한 확인
    if (!rule.include_domains.empty()) {
        bool domain_match = false;
        for (const auto& d : rule.include_domains) {
            if (page_domain == d || page_domain.ends_with("." + d)) {
                domain_match = true;
                break;
            }
        }
        if (!domain_match) return false;
    }

    // 제외 도메인 확인
    for (const auto& d : rule.exclude_domains) {
        if (page_domain == d || page_domain.ends_with("." + d)) {
            return false;
        }
    }

    // URL 패턴 매칭
    if (rule.has_regex) {
        try {
            return std::regex_search(url, rule.compiled_regex);
        } catch (...) {
            return false;
        }
    } else {
        // 단순 문자열 매칭 (폴백)
        return url.find(rule.pattern) != std::string::npos;
    }
}

// ============================================================
// Private: 인덱스 재구축
// ============================================================

void AdBlockEngine::rebuildIndices() {
    domain_index_ = FilterParser::buildDomainIndex(network_rules_);
    exception_domain_index_ = FilterParser::buildDomainIndex(exception_rules_);
}

// ============================================================
// Private: 내장 필터 로드
// ============================================================

void AdBlockEngine::loadBuiltinRules() {
    loadFilterText(BUILTIN_FILTERS, "Ordinal 내장 필터");
}

} // namespace ordinal::adblock
