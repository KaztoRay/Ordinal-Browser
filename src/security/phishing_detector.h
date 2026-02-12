#pragma once

/**
 * @file phishing_detector.h
 * @brief 피싱 탐지 엔진
 * 
 * URL 패턴 분석, 도메인 유사도 검사, 콘텐츠 분석을 통해
 * 피싱 사이트를 탐지합니다.
 */

#include "security_agent.h"

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <regex>

namespace ordinal::security {

/**
 * @brief 피싱 탐지 결과 상세
 */
struct PhishingAnalysis {
    double url_score{0.0};          ///< URL 기반 점수 (0~1)
    double content_score{0.0};      ///< 콘텐츠 기반 점수 (0~1)
    double domain_similarity{0.0};  ///< 유명 도메인과의 유사도
    std::string similar_domain;     ///< 가장 유사한 합법 도메인
    std::vector<std::string> indicators;  ///< 피싱 지표 목록
};

/**
 * @brief 피싱 탐지 엔진
 * 
 * 여러 휴리스틱과 패턴을 조합하여 피싱을 탐지합니다.
 */
class PhishingDetector {
public:
    PhishingDetector();
    ~PhishingDetector();

    /**
     * @brief 초기화 (블랙리스트/화이트리스트 로드)
     */
    bool initialize();

    /**
     * @brief URL 피싱 검사
     * @param url 검사할 URL
     * @return 위협 보고서 (안전하면 std::nullopt)
     */
    [[nodiscard]] std::optional<ThreatReport> checkUrl(const std::string& url);

    /**
     * @brief 페이지 콘텐츠 기반 피싱 분석
     * @param url 페이지 URL
     * @param html_content HTML 소스
     * @return 위협 보고서 목록
     */
    [[nodiscard]] std::vector<ThreatReport> analyzeContent(
        const std::string& url,
        const std::string& html_content
    );

    /**
     * @brief 상세 분석 결과
     */
    [[nodiscard]] PhishingAnalysis detailedAnalysis(
        const std::string& url,
        const std::string& html_content = ""
    );

    // ============================
    // 블랙리스트/화이트리스트 관리
    // ============================

    void addToBlacklist(const std::string& domain);
    void addToWhitelist(const std::string& domain);
    void removeFromBlacklist(const std::string& domain);
    void removeFromWhitelist(const std::string& domain);

    [[nodiscard]] bool isBlacklisted(const std::string& domain) const;
    [[nodiscard]] bool isWhitelisted(const std::string& domain) const;

private:
    /**
     * @brief URL에서 도메인 추출
     */
    [[nodiscard]] std::string extractDomain(const std::string& url) const;

    /**
     * @brief URL 기반 피싱 점수 계산
     * 
     * - IP 주소 사용 여부
     * - 의심스러운 포트
     * - URL 길이
     * - 서브도메인 수
     * - 특수 문자 비율
     * - 유명 도메인 오타 (typosquatting)
     */
    [[nodiscard]] double calculateUrlScore(const std::string& url) const;

    /**
     * @brief 콘텐츠 기반 피싱 점수 계산
     * 
     * - 로그인 폼 존재 여부
     * - 비밀번호 필드
     * - 외부 리소스 비율
     * - 의심스러운 키워드
     * - iframe 사용
     */
    [[nodiscard]] double calculateContentScore(const std::string& html) const;

    /**
     * @brief 도메인 유사도 계산 (레벤슈타인 거리 기반)
     */
    [[nodiscard]] double domainSimilarity(
        const std::string& domain,
        const std::string& target
    ) const;

    /**
     * @brief 레벤슈타인 편집 거리
     */
    [[nodiscard]] int levenshteinDistance(
        const std::string& s1,
        const std::string& s2
    ) const;

    // 유명 도메인 목록 (typosquatting 탐지용)
    std::vector<std::string> famous_domains_;

    // 블랙리스트/화이트리스트
    std::unordered_set<std::string> blacklist_;
    std::unordered_set<std::string> whitelist_;

    // 피싱 URL 패턴 (정규표현식)
    std::vector<std::regex> suspicious_patterns_;

    // 피싱 콘텐츠 키워드
    std::vector<std::string> phishing_keywords_;

    // 임계값
    double url_threshold_{0.6};         ///< URL 점수 임계값
    double content_threshold_{0.5};     ///< 콘텐츠 점수 임계값
    double similarity_threshold_{0.8};  ///< 도메인 유사도 임계값
};

} // namespace ordinal::security
