/**
 * @file privacy_tracker.cpp
 * @brief 프라이버시 추적기 차단 시스템 구현
 * 
 * 도메인 기반 블록리스트, URL 패턴 매칭, 핑거프린팅 API 탐지를 통해
 * 웹 추적을 차단합니다.
 */

#include "privacy_tracker.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace ordinal::security {

// ============================================================
// 생성자 / 소멸자
// ============================================================

PrivacyTracker::PrivacyTracker() = default;
PrivacyTracker::~PrivacyTracker() = default;

// ============================================================
// 초기화
// ============================================================

bool PrivacyTracker::initialize(const PrivacyConfig& config) {
    config_ = config;
    std::cout << "[PrivacyTracker] 프라이버시 보호 시스템 초기화 중..." << std::endl;

    // 알려진 추적기 도메인 등록 (주요 목록)
    // 광고 추적기
    std::vector<std::pair<std::string, TrackerType>> ad_trackers = {
        {"doubleclick.net", TrackerType::AdTracker},
        {"googlesyndication.com", TrackerType::AdTracker},
        {"googleadservices.com", TrackerType::AdTracker},
        {"adsense.google.com", TrackerType::AdTracker},
        {"adservice.google.com", TrackerType::AdTracker},
        {"moatads.com", TrackerType::AdTracker},
        {"adnxs.com", TrackerType::AdTracker},
        {"rubiconproject.com", TrackerType::AdTracker},
        {"pubmatic.com", TrackerType::AdTracker},
        {"openx.net", TrackerType::AdTracker},
        {"amazon-adsystem.com", TrackerType::AdTracker},
        {"criteo.com", TrackerType::AdTracker},
        {"taboola.com", TrackerType::AdTracker},
        {"outbrain.com", TrackerType::AdTracker},
    };

    // 분석 추적기
    std::vector<std::pair<std::string, TrackerType>> analytics_trackers = {
        {"google-analytics.com", TrackerType::AnalyticsTracker},
        {"googletagmanager.com", TrackerType::AnalyticsTracker},
        {"analytics.google.com", TrackerType::AnalyticsTracker},
        {"hotjar.com", TrackerType::SessionReplay},
        {"fullstory.com", TrackerType::SessionReplay},
        {"mouseflow.com", TrackerType::SessionReplay},
        {"clarity.ms", TrackerType::SessionReplay},
        {"mixpanel.com", TrackerType::AnalyticsTracker},
        {"segment.io", TrackerType::AnalyticsTracker},
        {"segment.com", TrackerType::AnalyticsTracker},
        {"amplitude.com", TrackerType::AnalyticsTracker},
        {"heap.io", TrackerType::AnalyticsTracker},
        {"newrelic.com", TrackerType::AnalyticsTracker},
        {"sentry.io", TrackerType::AnalyticsTracker},
    };

    // 소셜 추적기
    std::vector<std::pair<std::string, TrackerType>> social_trackers = {
        {"facebook.net", TrackerType::SocialTracker},
        {"fbcdn.net", TrackerType::SocialTracker},
        {"connect.facebook.com", TrackerType::SocialTracker},
        {"platform.twitter.com", TrackerType::SocialTracker},
        {"platform.linkedin.com", TrackerType::SocialTracker},
        {"snap.licdn.com", TrackerType::SocialTracker},
        {"widgets.pinterest.com", TrackerType::SocialTracker},
        {"platform.instagram.com", TrackerType::SocialTracker},
    };

    // 핑거프린팅 서비스
    std::vector<std::pair<std::string, TrackerType>> fingerprinters = {
        {"fingerprintjs.com", TrackerType::Fingerprinter},
        {"fpjs.io", TrackerType::Fingerprinter},
        {"fraudlogix.com", TrackerType::Fingerprinter},
        {"iovation.com", TrackerType::Fingerprinter},
        {"threatmetrix.com", TrackerType::Fingerprinter},
    };

    // 채굴 도메인
    std::vector<std::pair<std::string, TrackerType>> miners = {
        {"coinhive.com", TrackerType::CryptoMiner},
        {"coin-hive.com", TrackerType::CryptoMiner},
        {"jsecoin.com", TrackerType::CryptoMiner},
        {"authedmine.com", TrackerType::CryptoMiner},
        {"coinimp.com", TrackerType::CryptoMiner},
        {"crypto-loot.com", TrackerType::CryptoMiner},
        {"webmine.cz", TrackerType::CryptoMiner},
        {"mineralt.io", TrackerType::CryptoMiner},
    };

    // 모든 추적기 등록
    auto register_trackers = [this](const auto& trackers, const std::string& company_label) {
        for (const auto& [domain, type] : trackers) {
            blocked_domains_.insert(domain);
            domain_types_[domain] = type;
            domain_companies_[domain] = company_label;
        }
    };

    register_trackers(ad_trackers, "광고 네트워크");
    register_trackers(analytics_trackers, "분석 서비스");
    register_trackers(social_trackers, "소셜 미디어");
    register_trackers(fingerprinters, "핑거프린팅 서비스");
    register_trackers(miners, "암호화폐 채굴");

    // URL 패턴 기반 차단 규칙
    blocked_url_patterns_ = {
        // 광고 패턴
        std::regex(R"(/ads?/|/advert|/banner|/popup|/sponsor)", std::regex::icase),
        // 추적 픽셀
        std::regex(R"(\.(gif|png|jpg)\?.*?(utm_|track|pixel|beacon))", std::regex::icase),
        // 추적 스크립트
        std::regex(R"(/(tracker|tracking|analytics|telemetry)[\./])", std::regex::icase),
        // 핑거프린트 수집
        std::regex(R"(/(fingerprint|fp|device-id|browser-id)[\./])", std::regex::icase),
        // 1x1 이미지 비콘
        std::regex(R"(/pixel\.|/beacon\.|/imp\.|/impression)"),
        // 쿠키 동기화
        std::regex(R"(/(cookie-?sync|id-?sync|match|cm\.)\?)"),
    };

    // 핑거프린팅 API 탐지 패턴
    fingerprint_apis_ = {
        {FingerprintMethod::Canvas, {
            "toDataURL(", "getImageData(", "toBlob(",
            "canvas.getContext(\"2d\")", "canvas.getContext('2d')"
        }},
        {FingerprintMethod::WebGL, {
            "getContext(\"webgl\")", "getContext('webgl')",
            "getContext(\"experimental-webgl\")",
            "getParameter(", "getExtension(", "getSupportedExtensions(",
            "UNMASKED_VENDOR_WEBGL", "UNMASKED_RENDERER_WEBGL"
        }},
        {FingerprintMethod::AudioContext, {
            "AudioContext(", "OfflineAudioContext(",
            "createOscillator(", "createDynamicsCompressor(",
            "createAnalyser(", "getFloatFrequencyData("
        }},
        {FingerprintMethod::FontEnumeration, {
            "document.fonts", "FontFace(", "fonts.check(",
            "measureText(", "offsetWidth", "offsetHeight"
        }},
        {FingerprintMethod::ScreenResolution, {
            "screen.width", "screen.height", "screen.colorDepth",
            "screen.pixelDepth", "screen.availWidth", "screen.availHeight",
            "devicePixelRatio"
        }},
        {FingerprintMethod::TimezoneOffset, {
            "getTimezoneOffset()", "Intl.DateTimeFormat().resolvedOptions()",
            "Intl.DateTimeFormat"
        }},
        {FingerprintMethod::NavigatorProperties, {
            "navigator.userAgent", "navigator.platform", "navigator.vendor",
            "navigator.language", "navigator.languages",
            "navigator.hardwareConcurrency", "navigator.deviceMemory",
            "navigator.maxTouchPoints", "navigator.connection",
            "navigator.plugins", "navigator.mimeTypes"
        }},
        {FingerprintMethod::BatteryAPI, {
            "navigator.getBattery(", "BatteryManager"
        }},
        {FingerprintMethod::MediaDevices, {
            "navigator.mediaDevices.enumerateDevices(",
            "enumerateDevices(", "MediaDeviceInfo"
        }},
        {FingerprintMethod::WebRTC, {
            "RTCPeerConnection(", "createOffer(",
            "createDataChannel(", "onicecandidate"
        }},
    };

    // 사용자 정의 블록리스트 로드 (있는 경우)
    if (!config_.custom_blocklist_path.empty()) {
        loadBlocklist(config_.custom_blocklist_path);
    }

    std::cout << "[PrivacyTracker] ✓ 차단 도메인 " << blocked_domains_.size() << "개 로드" << std::endl;
    std::cout << "[PrivacyTracker] ✓ URL 패턴 " << blocked_url_patterns_.size() << "개 로드" << std::endl;
    std::cout << "[PrivacyTracker] ✓ 핑거프린팅 API " << fingerprint_apis_.size() << "가지 카테고리" << std::endl;
    return true;
}

// ============================================================
// 추적기 확인
// ============================================================

bool PrivacyTracker::isTracker(const std::string& url) const {
    std::string domain = extractDomain(url);

    // 허용 목록 확인
    if (allowed_domains_.contains(domain)) return false;

    // 도메인 블록리스트 확인
    if (blocked_domains_.contains(domain)) return true;

    // 상위 도메인도 확인 (subdomain.tracker.com → tracker.com)
    auto dot_pos = domain.find('.');
    while (dot_pos != std::string::npos) {
        std::string parent = domain.substr(dot_pos + 1);
        if (blocked_domains_.contains(parent)) return true;
        dot_pos = parent.find('.');
        if (dot_pos != std::string::npos) {
            domain = parent;
        } else {
            break;
        }
    }

    // URL 패턴 확인
    for (const auto& pattern : blocked_url_patterns_) {
        if (std::regex_search(url, pattern)) return true;
    }

    return false;
}

TrackerInfo PrivacyTracker::identifyTracker(const std::string& url) const {
    TrackerInfo info;
    std::string domain = extractDomain(url);
    info.domain = domain;

    // 도메인 매칭
    std::string check_domain = domain;
    while (!check_domain.empty()) {
        auto type_it = domain_types_.find(check_domain);
        if (type_it != domain_types_.end()) {
            info.type = type_it->second;

            auto company_it = domain_companies_.find(check_domain);
            if (company_it != domain_companies_.end()) {
                info.company = company_it->second;
            }

            // 카테고리 문자열
            switch (info.type) {
                case TrackerType::AdTracker:        info.category = "광고"; break;
                case TrackerType::AnalyticsTracker: info.category = "분석"; break;
                case TrackerType::SocialTracker:    info.category = "소셜"; break;
                case TrackerType::CookieTracker:    info.category = "쿠키"; break;
                case TrackerType::PixelTracker:     info.category = "픽셀"; break;
                case TrackerType::Fingerprinter:    info.category = "핑거프린팅"; break;
                case TrackerType::SessionReplay:    info.category = "세션 리플레이"; break;
                case TrackerType::CryptoMiner:      info.category = "채굴"; break;
                case TrackerType::Malicious:        info.category = "악성"; break;
                default:                            info.category = "알 수 없음"; break;
            }
            break;
        }

        // 상위 도메인 확인
        auto dot_pos = check_domain.find('.');
        if (dot_pos != std::string::npos) {
            check_domain = check_domain.substr(dot_pos + 1);
        } else {
            break;
        }
    }

    return info;
}

// ============================================================
// 요청 차단 판단
// ============================================================

bool PrivacyTracker::shouldBlock(
    const std::string& request_url,
    const std::string& page_url,
    const std::string& resource_type
) {
    // 허용 도메인 확인
    std::string request_domain = extractDomain(request_url);
    if (allowed_domains_.contains(request_domain)) return false;

    bool should_block = false;

    // 1. 추적기 도메인 확인
    if (config_.block_trackers && isTracker(request_url)) {
        should_block = true;
    }

    // 2. 서드파티 쿠키 차단
    if (config_.block_third_party_cookies && isThirdParty(request_url, page_url)) {
        // 서드파티 요청 중 추적 관련 요청만 차단
        if (isTracker(request_url)) {
            should_block = true;
        }
    }

    // 3. 픽셀 트래커 차단
    if (config_.block_pixel_trackers && isPixelTracker(request_url, resource_type)) {
        should_block = true;
    }

    // 4. 채굴 스크립트 차단
    if (config_.block_crypto_miners) {
        auto info = identifyTracker(request_url);
        if (info.type == TrackerType::CryptoMiner) {
            should_block = true;
            stats_.miners_blocked.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // 차단 시 통계 업데이트 및 기록
    if (should_block) {
        stats_.total_blocked.fetch_add(1, std::memory_order_relaxed);

        auto info = identifyTracker(request_url);
        info.is_blocked = true;

        switch (info.type) {
            case TrackerType::AdTracker:
                stats_.ads_blocked.fetch_add(1, std::memory_order_relaxed);
                break;
            case TrackerType::Fingerprinter:
                stats_.fingerprints_blocked.fetch_add(1, std::memory_order_relaxed);
                break;
            case TrackerType::CookieTracker:
                stats_.cookies_blocked.fetch_add(1, std::memory_order_relaxed);
                break;
            default:
                stats_.trackers_blocked.fetch_add(1, std::memory_order_relaxed);
                break;
        }

        // 페이지별 추적기 기록
        {
            std::lock_guard<std::mutex> lock(page_trackers_mutex_);
            page_trackers_[page_url].push_back(info);
        }
    }

    return should_block;
}

// ============================================================
// 핑거프린팅 탐지
// ============================================================

std::vector<FingerprintMethod> PrivacyTracker::detectFingerprinting(
    const std::string& script
) const {
    std::vector<FingerprintMethod> detected;

    for (const auto& [method, apis] : fingerprint_apis_) {
        int matches = 0;
        for (const auto& api : apis) {
            if (script.find(api) != std::string::npos) {
                matches++;
            }
        }

        // 카테고리 내 API 2개 이상 사용 시 핑거프린팅으로 판단
        if (matches >= 2) {
            detected.push_back(method);
        }
    }

    return detected;
}

// ============================================================
// 블록리스트 관리
// ============================================================

void PrivacyTracker::addBlockedDomain(const std::string& domain) {
    blocked_domains_.insert(domain);
}

void PrivacyTracker::addAllowedDomain(const std::string& domain) {
    allowed_domains_.insert(domain);
}

void PrivacyTracker::removeBlockedDomain(const std::string& domain) {
    blocked_domains_.erase(domain);
}

void PrivacyTracker::removeAllowedDomain(const std::string& domain) {
    allowed_domains_.erase(domain);
}

bool PrivacyTracker::loadBlocklist(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[PrivacyTracker] 블록리스트 로드 실패: " << filepath << std::endl;
        return false;
    }

    int loaded = 0;
    std::string line;
    while (std::getline(file, line)) {
        // 주석 및 빈 줄 건너뛰기
        if (line.empty() || line[0] == '!' || line[0] == '#' || line[0] == '[') {
            continue;
        }

        // EasyList 형식 간이 파싱
        // ||domain.com^ 형식 처리
        if (line.starts_with("||") && line.find('^') != std::string::npos) {
            std::string domain = line.substr(2);
            auto caret_pos = domain.find('^');
            if (caret_pos != std::string::npos) {
                domain = domain.substr(0, caret_pos);
            }
            if (!domain.empty()) {
                blocked_domains_.insert(domain);
                loaded++;
            }
        }
        // 순수 도메인 형식 (0.0.0.0 domain.com 또는 127.0.0.1 domain.com)
        else if (line.starts_with("0.0.0.0 ") || line.starts_with("127.0.0.1 ")) {
            auto space_pos = line.find(' ');
            if (space_pos != std::string::npos) {
                std::string domain = line.substr(space_pos + 1);
                // 후행 공백/주석 제거
                auto comment_pos = domain.find('#');
                if (comment_pos != std::string::npos) {
                    domain = domain.substr(0, comment_pos);
                }
                // 공백 제거
                while (!domain.empty() && std::isspace(static_cast<unsigned char>(domain.back()))) {
                    domain.pop_back();
                }
                if (!domain.empty() && domain != "localhost") {
                    blocked_domains_.insert(domain);
                    loaded++;
                }
            }
        }
    }

    std::cout << "[PrivacyTracker] ✓ 블록리스트 " << loaded << "개 도메인 로드: " << filepath << std::endl;
    return true;
}

// ============================================================
// 통계
// ============================================================

std::vector<TrackerInfo> PrivacyTracker::getBlockedTrackersForPage(
    const std::string& page_url
) const {
    std::lock_guard<std::mutex> lock(page_trackers_mutex_);
    auto it = page_trackers_.find(page_url);
    if (it != page_trackers_.end()) {
        return it->second;
    }
    return {};
}

void PrivacyTracker::resetStats() {
    stats_.total_blocked.store(0, std::memory_order_relaxed);
    stats_.trackers_blocked.store(0, std::memory_order_relaxed);
    stats_.ads_blocked.store(0, std::memory_order_relaxed);
    stats_.fingerprints_blocked.store(0, std::memory_order_relaxed);
    stats_.cookies_blocked.store(0, std::memory_order_relaxed);
    stats_.miners_blocked.store(0, std::memory_order_relaxed);
}

// ============================================================
// 내부 유틸리티
// ============================================================

std::string PrivacyTracker::extractDomain(const std::string& url) const {
    std::string domain = url;

    // 프로토콜 제거
    auto protocol_end = domain.find("://");
    if (protocol_end != std::string::npos) {
        domain = domain.substr(protocol_end + 3);
    }

    // 포트/경로 제거
    auto slash_pos = domain.find('/');
    if (slash_pos != std::string::npos) {
        domain = domain.substr(0, slash_pos);
    }

    auto colon_pos = domain.find(':');
    if (colon_pos != std::string::npos) {
        domain = domain.substr(0, colon_pos);
    }

    // 소문자 변환
    std::transform(domain.begin(), domain.end(), domain.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return domain;
}

bool PrivacyTracker::isThirdParty(
    const std::string& request_url,
    const std::string& page_url
) const {
    std::string request_domain = getRegistrableDomain(extractDomain(request_url));
    std::string page_domain = getRegistrableDomain(extractDomain(page_url));

    return request_domain != page_domain;
}

std::string PrivacyTracker::getRegistrableDomain(const std::string& domain) const {
    // 간이 eTLD+1 추출 (실제로는 Public Suffix List 사용 필요)
    // "sub.example.com" → "example.com"
    // "sub.example.co.kr" → "example.co.kr"

    // 2단계 TLD 목록
    std::vector<std::string> two_level_tlds = {
        ".co.kr", ".co.jp", ".co.uk", ".com.br", ".com.au",
        ".com.cn", ".net.cn", ".org.cn", ".ac.kr", ".or.kr",
        ".go.kr", ".ne.kr", ".re.kr", ".pe.kr"
    };

    for (const auto& tld : two_level_tlds) {
        if (domain.length() > tld.length() &&
            domain.compare(domain.length() - tld.length(), tld.length(), tld) == 0) {
            // TLD 앞의 마지막 구성 요소 포함
            std::string prefix = domain.substr(0, domain.length() - tld.length());
            auto last_dot = prefix.rfind('.');
            if (last_dot != std::string::npos) {
                return prefix.substr(last_dot + 1) + tld;
            }
            return domain;
        }
    }

    // 기본: 마지막 두 구성 요소
    auto last_dot = domain.rfind('.');
    if (last_dot == std::string::npos) return domain;

    auto second_last = domain.rfind('.', last_dot - 1);
    if (second_last == std::string::npos) return domain;

    return domain.substr(second_last + 1);
}

bool PrivacyTracker::isPixelTracker(
    const std::string& url,
    const std::string& resource_type
) const {
    // 리소스 타입이 이미지인 경우
    if (resource_type == "image" || resource_type == "img") {
        // 추적 관련 URL 패턴 확인
        std::string lower = url;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // 일반적인 픽셀 트래커 패턴
        if (lower.find("/pixel") != std::string::npos ||
            lower.find("/beacon") != std::string::npos ||
            lower.find("/imp") != std::string::npos ||
            lower.find("/track") != std::string::npos ||
            lower.find("1x1") != std::string::npos ||
            lower.find("spacer.gif") != std::string::npos ||
            lower.find("blank.gif") != std::string::npos) {
            return true;
        }

        // 추적 파라미터가 있는 이미지
        if ((lower.find(".gif?") != std::string::npos ||
             lower.find(".png?") != std::string::npos) &&
            (lower.find("id=") != std::string::npos ||
             lower.find("uid=") != std::string::npos ||
             lower.find("sid=") != std::string::npos ||
             lower.find("track=") != std::string::npos)) {
            return true;
        }
    }

    return false;
}

} // namespace ordinal::security
