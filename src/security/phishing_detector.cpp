/**
 * @file phishing_detector.cpp
 * @brief 피싱 탐지 엔진 구현
 * 
 * URL 패턴 분석, 레벤슈타인 거리 기반 타이포스쿼팅 탐지,
 * 콘텐츠 기반 피싱 패턴 감지를 수행합니다.
 */

#include "phishing_detector.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <numeric>
#include <sstream>

namespace ordinal::security {

// ============================================================
// 생성자 / 소멸자
// ============================================================

PhishingDetector::PhishingDetector() = default;
PhishingDetector::~PhishingDetector() = default;

// ============================================================
// 초기화
// ============================================================

bool PhishingDetector::initialize() {
    std::cout << "[PhishingDetector] 피싱 탐지기 초기화 중..." << std::endl;

    // 유명 도메인 목록 (타이포스쿼팅 탐지용)
    famous_domains_ = {
        "google.com", "facebook.com", "amazon.com", "apple.com",
        "microsoft.com", "netflix.com", "paypal.com", "twitter.com",
        "instagram.com", "linkedin.com", "github.com", "yahoo.com",
        "outlook.com", "hotmail.com", "gmail.com", "youtube.com",
        "whatsapp.com", "telegram.org", "discord.com", "reddit.com",
        "twitch.tv", "spotify.com", "dropbox.com", "adobe.com",
        "chase.com", "wellsfargo.com", "bankofamerica.com",
        "citibank.com", "americanexpress.com", "capitalone.com",
        "naver.com", "daum.net", "kakao.com", "samsung.com",
        "coupang.com", "toss.im", "kakaopay.com", "naverpay.com",
        "shinhan.com", "kbstar.com", "wooribank.com", "hanabank.com",
        "ibk.co.kr", "nhbank.com"
    };

    // 의심스러운 URL 패턴 (정규 표현식)
    suspicious_patterns_ = {
        // IP 주소 기반 URL
        std::regex(R"(https?://\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})"),
        // 비표준 포트 사용
        std::regex(R"(https?://[^/]+:\d{2,5}/)"),
        // 긴 서브도메인 (4개 이상)
        std::regex(R"(https?://([^.]+\.){4,}[^/]+)"),
        // URL에 @ 기호 사용 (리다이렉션 트릭)
        std::regex(R"(https?://[^@]+@[^/]+)"),
        // data: URI 스킴
        std::regex(R"(data:text/html)"),
        // 인코딩된 문자가 많은 URL
        std::regex(R"(%[0-9a-fA-F]{2}.*%[0-9a-fA-F]{2}.*%[0-9a-fA-F]{2}.*%[0-9a-fA-F]{2})"),
        // 유명 브랜드명이 서브도메인에 포함 (가짜 사이트)
        std::regex(R"(https?://(login|secure|account|verify|update|confirm)[.-])"),
        // 이중 확장자/경로 위장
        std::regex(R"(\.(php|asp|cgi)\?.*=https?://)"),
        // 한국 은행 사칭 패턴
        std::regex(R"(https?://[^/]*(bank|pay|toss|kakao)[^.]*\.(xyz|top|club|info|pw))"),
        // 하이픈을 과도하게 사용한 도메인
        std::regex(R"(https?://[^/]*-[^/]*-[^/]*-[^/]*\.)"),
    };

    // 피싱 콘텐츠 키워드
    phishing_keywords_ = {
        // 영문 키워드
        "verify your account", "confirm your identity", "update your information",
        "unusual activity", "suspended account", "click here immediately",
        "your account will be closed", "unauthorized transaction",
        "security alert", "confirm your password", "expire",
        "act now", "limited time", "you have been selected",
        "congratulations you won", "claim your prize",
        // 한국어 키워드
        "계정 확인", "본인 인증", "비밀번호 변경", "긴급 공지",
        "계정 정지", "보안 업데이트", "택배 확인", "미수금 안내",
        "당첨 축하", "긴급 결제", "개인정보 확인", "출석 확인",
        "정부지원금", "환급금 안내", "카드 승인", "입금 확인",
        "본인확인", "계좌 정지", "접속 이력"
    };

    std::cout << "[PhishingDetector] ✓ 유명 도메인 " << famous_domains_.size() << "개 로드" << std::endl;
    std::cout << "[PhishingDetector] ✓ 의심 패턴 " << suspicious_patterns_.size() << "개 로드" << std::endl;
    std::cout << "[PhishingDetector] ✓ 피싱 키워드 " << phishing_keywords_.size() << "개 로드" << std::endl;
    return true;
}

// ============================================================
// URL 피싱 검사
// ============================================================

std::optional<ThreatReport> PhishingDetector::checkUrl(const std::string& url) {
    std::string domain = extractDomain(url);

    // 화이트리스트 확인 (즉시 통과)
    if (isWhitelisted(domain)) {
        return std::nullopt;
    }

    // 블랙리스트 확인 (즉시 차단)
    if (isBlacklisted(domain)) {
        ThreatReport report;
        report.category = ThreatCategory::Phishing;
        report.severity = ThreatSeverity::Critical;
        report.url = url;
        report.description = "블랙리스트에 등록된 피싱 도메인입니다: " + domain;
        report.recommendation = "이 사이트에 접속하지 마세요.";
        report.detector_name = "PhishingDetector";
        report.confidence = 1.0;
        report.detected_at = std::chrono::system_clock::now();
        return report;
    }

    // URL 기반 피싱 점수 계산
    double url_score = calculateUrlScore(url);

    // 도메인 유사도 검사 (타이포스쿼팅)
    double max_similarity = 0.0;
    std::string most_similar;

    for (const auto& famous : famous_domains_) {
        double sim = domainSimilarity(domain, famous);
        if (sim > max_similarity) {
            max_similarity = sim;
            most_similar = famous;
        }
    }

    // 타이포스쿼팅 감지: 유사하지만 동일하지 않은 도메인
    bool is_typosquat = (max_similarity >= similarity_threshold_) && (domain != most_similar);
    if (is_typosquat) {
        url_score += 0.3;  // 타이포스쿼팅 가중치 부여
    }

    // 임계값 초과 시 위협 보고
    if (url_score >= url_threshold_) {
        ThreatReport report;
        report.category = ThreatCategory::Phishing;
        report.url = url;
        report.detector_name = "PhishingDetector";
        report.detected_at = std::chrono::system_clock::now();
        report.confidence = std::min(url_score, 1.0);

        if (url_score >= 0.9) {
            report.severity = ThreatSeverity::Critical;
            report.description = "매우 높은 피싱 확률이 감지되었습니다.";
        } else if (url_score >= 0.75) {
            report.severity = ThreatSeverity::High;
            report.description = "높은 피싱 확률이 감지되었습니다.";
        } else {
            report.severity = ThreatSeverity::Medium;
            report.description = "의심스러운 URL 패턴이 감지되었습니다.";
        }

        if (is_typosquat) {
            report.description += " ('" + most_similar + "' 사칭 의심)";
            report.metadata["similar_domain"] = most_similar;
            report.metadata["similarity"] = std::to_string(max_similarity);
        }

        report.recommendation = "이 사이트의 진위를 확인하세요. 개인정보를 입력하지 마세요.";
        return report;
    }

    return std::nullopt;
}

// ============================================================
// 콘텐츠 기반 피싱 분석
// ============================================================

std::vector<ThreatReport> PhishingDetector::analyzeContent(
    const std::string& url,
    const std::string& html_content
) {
    std::vector<ThreatReport> threats;

    if (html_content.empty()) return threats;

    double content_score = calculateContentScore(html_content);

    if (content_score >= content_threshold_) {
        ThreatReport report;
        report.category = ThreatCategory::Phishing;
        report.url = url;
        report.detector_name = "PhishingDetector/ContentAnalysis";
        report.detected_at = std::chrono::system_clock::now();
        report.confidence = std::min(content_score, 1.0);

        if (content_score >= 0.85) {
            report.severity = ThreatSeverity::High;
            report.description = "페이지 콘텐츠에서 강력한 피싱 패턴이 감지되었습니다.";
        } else if (content_score >= 0.65) {
            report.severity = ThreatSeverity::Medium;
            report.description = "페이지 콘텐츠에서 의심스러운 피싱 패턴이 감지되었습니다.";
        } else {
            report.severity = ThreatSeverity::Low;
            report.description = "페이지 콘텐츠에서 약한 피싱 지표가 감지되었습니다.";
        }

        report.recommendation = "이 페이지에 개인정보를 입력하지 마세요.";
        report.metadata["content_score"] = std::to_string(content_score);
        threats.push_back(report);
    }

    return threats;
}

// ============================================================
// 상세 분석
// ============================================================

PhishingAnalysis PhishingDetector::detailedAnalysis(
    const std::string& url,
    const std::string& html_content
) {
    PhishingAnalysis analysis;

    // URL 점수 계산
    analysis.url_score = calculateUrlScore(url);

    // 콘텐츠 점수 계산 (HTML이 있는 경우)
    if (!html_content.empty()) {
        analysis.content_score = calculateContentScore(html_content);
    }

    // 도메인 유사도 분석
    std::string domain = extractDomain(url);
    double max_similarity = 0.0;

    for (const auto& famous : famous_domains_) {
        double sim = domainSimilarity(domain, famous);
        if (sim > max_similarity) {
            max_similarity = sim;
            analysis.similar_domain = famous;
        }
    }
    analysis.domain_similarity = max_similarity;

    // 지표 수집
    if (analysis.url_score > 0.3) {
        analysis.indicators.push_back("의심스러운 URL 구조 (점수: " + 
            std::to_string(analysis.url_score) + ")");
    }
    if (analysis.content_score > 0.3) {
        analysis.indicators.push_back("의심스러운 콘텐츠 패턴 (점수: " + 
            std::to_string(analysis.content_score) + ")");
    }
    if (max_similarity >= similarity_threshold_ && domain != analysis.similar_domain) {
        analysis.indicators.push_back("타이포스쿼팅 의심: '" + domain + 
            "' ≈ '" + analysis.similar_domain + "' (유사도: " + 
            std::to_string(max_similarity) + ")");
    }

    return analysis;
}

// ============================================================
// 블랙리스트 / 화이트리스트 관리
// ============================================================

void PhishingDetector::addToBlacklist(const std::string& domain) {
    blacklist_.insert(domain);
}

void PhishingDetector::addToWhitelist(const std::string& domain) {
    whitelist_.insert(domain);
}

void PhishingDetector::removeFromBlacklist(const std::string& domain) {
    blacklist_.erase(domain);
}

void PhishingDetector::removeFromWhitelist(const std::string& domain) {
    whitelist_.erase(domain);
}

bool PhishingDetector::isBlacklisted(const std::string& domain) const {
    return blacklist_.contains(domain);
}

bool PhishingDetector::isWhitelisted(const std::string& domain) const {
    return whitelist_.contains(domain);
}

// ============================================================
// 내부 헬퍼 메서드
// ============================================================

std::string PhishingDetector::extractDomain(const std::string& url) const {
    std::string domain = url;

    // 프로토콜 제거
    auto protocol_end = domain.find("://");
    if (protocol_end != std::string::npos) {
        domain = domain.substr(protocol_end + 3);
    }

    // @ 이후 부분 사용 (URL 난독화 트릭 대응)
    auto at_pos = domain.find('@');
    if (at_pos != std::string::npos) {
        domain = domain.substr(at_pos + 1);
    }

    // 포트/경로/쿼리 제거
    auto port_pos = domain.find(':');
    if (port_pos != std::string::npos) {
        domain = domain.substr(0, port_pos);
    }

    auto path_pos = domain.find('/');
    if (path_pos != std::string::npos) {
        domain = domain.substr(0, path_pos);
    }

    auto query_pos = domain.find('?');
    if (query_pos != std::string::npos) {
        domain = domain.substr(0, query_pos);
    }

    // 소문자 변환
    std::transform(domain.begin(), domain.end(), domain.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return domain;
}

double PhishingDetector::calculateUrlScore(const std::string& url) const {
    double score = 0.0;
    std::string domain = extractDomain(url);

    // 1. IP 주소 기반 URL 검사 (+0.25)
    std::regex ip_regex(R"(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})");
    if (std::regex_match(domain, ip_regex)) {
        score += 0.25;
    }

    // 2. URL 길이 검사 (긴 URL은 의심)
    if (url.length() > 75) score += 0.1;
    if (url.length() > 150) score += 0.15;
    if (url.length() > 250) score += 0.1;

    // 3. 서브도메인 수 검사 (많으면 의심)
    int dot_count = static_cast<int>(std::count(domain.begin(), domain.end(), '.'));
    if (dot_count > 3) score += 0.15;
    if (dot_count > 5) score += 0.15;

    // 4. 특수 문자 비율 검사
    int special_chars = 0;
    for (char c : url) {
        if (c == '-' || c == '_' || c == '@' || c == '~' || c == '%') {
            special_chars++;
        }
    }
    double special_ratio = static_cast<double>(special_chars) / static_cast<double>(url.length());
    if (special_ratio > 0.1) score += 0.1;
    if (special_ratio > 0.2) score += 0.15;

    // 5. @ 기호 사용 (리다이렉션 트릭)
    if (url.find('@') != std::string::npos) {
        score += 0.3;
    }

    // 6. 의심스러운 TLD 검사
    std::vector<std::string> suspicious_tlds = {
        ".xyz", ".top", ".club", ".info", ".pw", ".tk", ".ml",
        ".ga", ".cf", ".gq", ".buzz", ".work", ".click", ".loan",
        ".racing", ".win", ".bid", ".stream", ".download"
    };
    for (const auto& tld : suspicious_tlds) {
        if (domain.length() >= tld.length() &&
            domain.compare(domain.length() - tld.length(), tld.length(), tld) == 0) {
            score += 0.15;
            break;
        }
    }

    // 7. HTTPS 미사용 (로그인 페이지 등에서 위험)
    if (url.starts_with("http://")) {
        score += 0.1;
    }

    // 8. 정규 표현식 패턴 매치
    for (const auto& pattern : suspicious_patterns_) {
        if (std::regex_search(url, pattern)) {
            score += 0.1;
        }
    }

    // 9. 하이픈 과다 사용 (도메인에서)
    int hyphen_count = static_cast<int>(std::count(domain.begin(), domain.end(), '-'));
    if (hyphen_count > 2) score += 0.1;
    if (hyphen_count > 4) score += 0.15;

    // 10. 숫자가 도메인에 과도하게 포함
    int digit_count = 0;
    for (char c : domain) {
        if (std::isdigit(static_cast<unsigned char>(c))) digit_count++;
    }
    double digit_ratio = static_cast<double>(digit_count) / static_cast<double>(domain.length());
    if (digit_ratio > 0.3) score += 0.1;

    // 점수 상한 1.0
    return std::min(score, 1.0);
}

double PhishingDetector::calculateContentScore(const std::string& html) const {
    double score = 0.0;
    // 소문자 변환된 HTML (검색 편의를 위해)
    std::string lower_html = html;
    std::transform(lower_html.begin(), lower_html.end(), lower_html.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // 1. 로그인 폼 존재 여부
    bool has_password_field = (lower_html.find("type=\"password\"") != std::string::npos) ||
                              (lower_html.find("type='password'") != std::string::npos);
    bool has_form = lower_html.find("<form") != std::string::npos;

    if (has_password_field && has_form) {
        score += 0.2;  // 로그인 폼은 그 자체로 피싱 지표는 아니지만 가중치 부여
    }

    // 2. 폼 액션이 외부 도메인으로 향하는지 검사
    std::regex form_action_regex(R"(<form[^>]*action\s*=\s*["']https?://[^"']+["'])");
    if (std::regex_search(lower_html, form_action_regex)) {
        score += 0.15;
    }

    // 3. 숨겨진 입력 필드가 많은지 검사
    int hidden_inputs = 0;
    std::string::size_type pos = 0;
    while ((pos = lower_html.find("type=\"hidden\"", pos)) != std::string::npos) {
        hidden_inputs++;
        pos += 13;
    }
    if (hidden_inputs > 5) score += 0.1;
    if (hidden_inputs > 10) score += 0.1;

    // 4. iframe 사용 (투명/숨김 iframe은 위험)
    if (lower_html.find("<iframe") != std::string::npos) {
        score += 0.1;
        // 숨겨진 iframe
        if (lower_html.find("display:none") != std::string::npos ||
            lower_html.find("visibility:hidden") != std::string::npos ||
            lower_html.find("height:0") != std::string::npos ||
            lower_html.find("width:0") != std::string::npos) {
            score += 0.15;
        }
    }

    // 5. 피싱 키워드 매치
    int keyword_matches = 0;
    for (const auto& keyword : phishing_keywords_) {
        // 키워드도 소문자로 비교
        std::string lower_kw = keyword;
        std::transform(lower_kw.begin(), lower_kw.end(), lower_kw.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lower_html.find(lower_kw) != std::string::npos) {
            keyword_matches++;
        }
    }
    // 키워드 매칭 수에 비례하여 점수 부여
    score += std::min(static_cast<double>(keyword_matches) * 0.05, 0.3);

    // 6. 외부 리소스 비율 (정상 사이트는 자체 CDN 사용)
    int external_links = 0;
    std::regex ext_regex(R"(src\s*=\s*["']https?://[^"']+["'])");
    auto links_begin = std::sregex_iterator(lower_html.begin(), lower_html.end(), ext_regex);
    auto links_end = std::sregex_iterator();
    external_links = static_cast<int>(std::distance(links_begin, links_end));

    if (external_links > 20) score += 0.1;

    // 7. JavaScript 리다이렉트 (document.location, window.location)
    if (lower_html.find("document.location") != std::string::npos ||
        lower_html.find("window.location") != std::string::npos ||
        lower_html.find("meta http-equiv=\"refresh\"") != std::string::npos) {
        score += 0.1;
    }

    // 8. 우클릭 / 개발자 도구 비활성화 시도
    if (lower_html.find("oncontextmenu") != std::string::npos &&
        lower_html.find("return false") != std::string::npos) {
        score += 0.15;
    }

    // 9. Base64 인코딩된 이미지가 과도하게 많음 (로고 복제)
    int base64_images = 0;
    pos = 0;
    while ((pos = lower_html.find("data:image", pos)) != std::string::npos) {
        base64_images++;
        pos += 10;
    }
    if (base64_images > 5) score += 0.1;

    // 10. 페이지 내용이 극도로 적음 (원격 콘텐츠 로드)
    if (html.length() < 500 && has_form) {
        score += 0.15;
    }

    return std::min(score, 1.0);
}

double PhishingDetector::domainSimilarity(
    const std::string& domain,
    const std::string& target
) const {
    if (domain == target) return 1.0;
    if (domain.empty() || target.empty()) return 0.0;

    int distance = levenshteinDistance(domain, target);
    int max_len = static_cast<int>(std::max(domain.length(), target.length()));

    if (max_len == 0) return 1.0;

    // 유사도 = 1 - (편집 거리 / 최대 길이)
    double similarity = 1.0 - static_cast<double>(distance) / static_cast<double>(max_len);

    // 보너스: 서브스트링 포함 관계 (예: "gogle.com"에 "google" 부분 포함)
    std::string domain_base = domain.substr(0, domain.find('.'));
    std::string target_base = target.substr(0, target.find('.'));

    if (domain_base.find(target_base) != std::string::npos ||
        target_base.find(domain_base) != std::string::npos) {
        similarity = std::max(similarity, 0.85);
    }

    // 문자 치환 탐지 (예: 'l' → '1', 'o' → '0')
    // 호모글리프(동형이의자) 대체 탐지
    std::unordered_map<char, char> homoglyphs = {
        {'0', 'o'}, {'1', 'l'}, {'1', 'i'}, {'5', 's'},
        {'8', 'b'}, {'3', 'e'}, {'6', 'g'}, {'9', 'q'}
    };

    std::string normalized_domain = domain_base;
    for (auto& c : normalized_domain) {
        auto it = homoglyphs.find(c);
        if (it != homoglyphs.end()) {
            c = it->second;
        }
    }

    if (normalized_domain == target_base) {
        similarity = std::max(similarity, 0.95);
    }

    return similarity;
}

int PhishingDetector::levenshteinDistance(
    const std::string& s1,
    const std::string& s2
) const {
    const size_t m = s1.length();
    const size_t n = s2.length();

    // DP 테이블 (공간 최적화: 2행만 유지)
    std::vector<int> prev(n + 1);
    std::vector<int> curr(n + 1);

    // 초기화
    for (size_t j = 0; j <= n; ++j) {
        prev[j] = static_cast<int>(j);
    }

    // 동적 프로그래밍으로 편집 거리 계산
    for (size_t i = 1; i <= m; ++i) {
        curr[0] = static_cast<int>(i);
        for (size_t j = 1; j <= n; ++j) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            curr[j] = std::min({
                prev[j] + 1,        // 삭제
                curr[j - 1] + 1,    // 삽입
                prev[j - 1] + cost  // 대체
            });

            // 전치(transposition) 검사 - Damerau-Levenshtein 확장
            if (i > 1 && j > 1 && 
                s1[i - 1] == s2[j - 2] && s1[i - 2] == s2[j - 1]) {
                curr[j] = std::min(curr[j], static_cast<int>(prev[j - 1]));
            }
        }
        std::swap(prev, curr);
    }

    return prev[n];
}

} // namespace ordinal::security
