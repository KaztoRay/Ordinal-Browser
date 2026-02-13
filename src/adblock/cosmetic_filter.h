#pragma once

/**
 * @file cosmetic_filter.h
 * @brief 코스메틱(CSS) 필터 엔진
 *
 * ##.ad-banner, domain##.ad 형태의 CSS 셀렉터 추출,
 * 확장 CSS (:has(), :has-text(), :not()), 도메인 특정 코스메틱 룰,
 * 일반(generic) 코스메틱 룰, 결합 CSS 스타일시트 생성.
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace ordinal::adblock {

// ============================================================
// 코스메틱 룰 구조
// ============================================================

/**
 * @brief 코스메틱 필터 룰
 */
struct CosmeticRule {
    std::string raw_text;                       ///< 원본 필터 문자열
    std::string css_selector;                   ///< CSS 셀렉터
    std::vector<std::string> domains;           ///< 적용 도메인 (비어있으면 전체)
    std::vector<std::string> exclude_domains;   ///< 제외 도메인
    bool is_exception{false};                   ///< #@# 예외 룰인지
    bool is_extended{false};                    ///< 확장 CSS 사용 여부
    bool is_script_inject{false};               ///< 스크립트 인젝션인지 (##+js)
    std::string script_code;                    ///< 인젝션 스크립트 코드
};

// ============================================================
// CosmeticFilter 클래스
// ============================================================

/**
 * @brief 코스메틱 필터 엔진
 *
 * 필터 리스트에서 CSS 숨김 룰을 추출하고,
 * 페이지 도메인에 맞는 결합 CSS 스타일시트를 생성합니다.
 */
class CosmeticFilter {
public:
    CosmeticFilter();
    ~CosmeticFilter();

    /**
     * @brief 필터 리스트 텍스트에서 코스메틱 룰 추출
     * @param text 필터 리스트 텍스트
     * @return 추출된 코스메틱 룰 수
     */
    int parseFilterList(const std::string& text);

    /**
     * @brief 단일 라인 파싱
     * @param line 필터 라인
     * @return 파싱된 룰 (코스메틱이 아니면 nullopt)
     */
    std::optional<CosmeticRule> parseLine(const std::string& line) const;

    /**
     * @brief 룰 추가
     */
    void addRule(const CosmeticRule& rule);

    /**
     * @brief 도메인에 적용할 CSS 스타일시트 생성
     * @param domain 현재 페이지 도메인
     * @return CSS 스타일시트 문자열
     */
    [[nodiscard]] std::string generateStylesheet(const std::string& domain) const;

    /**
     * @brief 도메인에 적용할 셀렉터 목록 반환
     * @param domain 현재 페이지 도메인
     * @return CSS 셀렉터 목록
     */
    [[nodiscard]] std::vector<std::string> getSelectorsForDomain(
        const std::string& domain) const;

    /**
     * @brief 확장 CSS 룰 목록 (도메인 기반)
     * @param domain 현재 페이지 도메인
     * @return 확장 CSS 룰 목록
     */
    [[nodiscard]] std::vector<CosmeticRule> getExtendedRulesForDomain(
        const std::string& domain) const;

    /**
     * @brief 총 룰 수
     */
    [[nodiscard]] int totalRuleCount() const;

    /**
     * @brief 일반 룰 수
     */
    [[nodiscard]] int genericRuleCount() const;

    /**
     * @brief 도메인 특정 룰 수
     */
    [[nodiscard]] int domainSpecificRuleCount() const;

    /**
     * @brief 모든 룰 초기화
     */
    void clear();

private:
    /// 일반(generic) 코스메틱 룰 — 모든 페이지에 적용
    std::vector<CosmeticRule> generic_rules_;

    /// 도메인 특정 룰 — domain → rules 맵
    std::unordered_map<std::string, std::vector<CosmeticRule>> domain_rules_;

    /// 예외 룰 (도메인별)
    std::unordered_map<std::string, std::unordered_set<std::string>> exception_selectors_;

    /// 전체 예외 셀렉터 (모든 도메인)
    std::unordered_set<std::string> global_exception_selectors_;

    /**
     * @brief 도메인이 룰의 도메인 조건에 매칭되는지 확인
     */
    static bool domainMatches(const std::string& page_domain,
                               const std::vector<std::string>& rule_domains);

    /**
     * @brief 셀렉터가 예외 목록에 있는지 확인
     */
    bool isExcepted(const std::string& selector,
                     const std::string& domain) const;

    /**
     * @brief 확장 CSS인지 확인 (:has, :has-text, :not 등)
     */
    static bool isExtendedCSS(const std::string& selector);

    /**
     * @brief 등록 가능 도메인 추출
     */
    static std::string getRegistrableDomain(const std::string& domain);
};

} // namespace ordinal::adblock
