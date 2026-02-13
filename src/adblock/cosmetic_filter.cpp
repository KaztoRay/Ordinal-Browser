/**
 * @file cosmetic_filter.cpp
 * @brief 코스메틱(CSS) 필터 엔진 구현
 *
 * CSS 셀렉터 추출 (##.ad-banner, domain##.ad),
 * 확장 CSS (:has(), :has-text(), :not()),
 * 도메인 특정/일반 룰 분류, 결합 스타일시트 생성.
 */

#include "cosmetic_filter.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace ordinal::adblock {

namespace {

/// 소문자 변환
std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

/// 간이 eTLD+1 추출
std::string simpleRegistrableDomain(const std::string& domain) {
    std::string d = domain;
    if (!d.empty() && d[0] == '.') d = d.substr(1);

    std::vector<std::string> parts;
    std::istringstream iss(d);
    std::string part;
    while (std::getline(iss, part, '.')) {
        if (!part.empty()) parts.push_back(part);
    }

    if (parts.size() <= 2) return d;
    return parts[parts.size() - 2] + "." + parts.back();
}

} // anonymous namespace

// ============================================================
// 생성자 / 소멸자
// ============================================================

CosmeticFilter::CosmeticFilter() = default;
CosmeticFilter::~CosmeticFilter() = default;

// ============================================================
// 필터 리스트 파싱
// ============================================================

int CosmeticFilter::parseFilterList(const std::string& text) {
    int count = 0;

    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        // 줄 끝 정리
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }

        auto rule = parseLine(line);
        if (rule) {
            addRule(*rule);
            count++;
        }
    }

    return count;
}

// ============================================================
// 단일 라인 파싱
// ============================================================

std::optional<CosmeticRule> CosmeticFilter::parseLine(const std::string& line) const {
    if (line.empty()) return std::nullopt;

    // 주석 건너뛰기
    if (line[0] == '!' || line[0] == '[') return std::nullopt;

    CosmeticRule rule;
    rule.raw_text = line;

    // ## / #@# / #?# 위치 찾기
    std::string separator;
    size_t sep_pos = std::string::npos;

    // #@# 예외 (우선 확인)
    sep_pos = line.find("#@#");
    if (sep_pos != std::string::npos) {
        separator = "#@#";
        rule.is_exception = true;
    }

    // #?# 확장 CSS
    if (sep_pos == std::string::npos) {
        sep_pos = line.find("#?#");
        if (sep_pos != std::string::npos) {
            separator = "#?#";
            rule.is_extended = true;
        }
    }

    // ## 일반
    if (sep_pos == std::string::npos) {
        sep_pos = line.find("##");
        if (sep_pos != std::string::npos) {
            separator = "##";
        }
    }

    // 코스메틱 필터가 아님
    if (sep_pos == std::string::npos) {
        return std::nullopt;
    }

    // 도메인 부분 (## 앞)
    std::string domain_part = line.substr(0, sep_pos);

    // 셀렉터 부분 (## 뒤)
    std::string selector = line.substr(sep_pos + separator.size());

    if (selector.empty()) {
        return std::nullopt;
    }

    // 스크립트 인젝션 확인 (#+js(...))
    if (selector.starts_with("+js(") && selector.back() == ')') {
        rule.is_script_inject = true;
        rule.script_code = selector.substr(4, selector.size() - 5);
    }

    rule.css_selector = selector;

    // 확장 CSS 확인
    if (!rule.is_extended) {
        rule.is_extended = isExtendedCSS(selector);
    }

    // 도메인 파싱 (콤마 구분)
    if (!domain_part.empty()) {
        std::istringstream dss(domain_part);
        std::string domain;
        while (std::getline(dss, domain, ',')) {
            // 공백 제거
            auto s = domain.find_first_not_of(" ");
            if (s != std::string::npos) domain = domain.substr(s);
            auto e = domain.find_last_not_of(" ");
            if (e != std::string::npos) domain = domain.substr(0, e + 1);

            if (domain.empty()) continue;

            if (domain.starts_with("~")) {
                rule.exclude_domains.push_back(toLower(domain.substr(1)));
            } else {
                rule.domains.push_back(toLower(domain));
            }
        }
    }

    return rule;
}

// ============================================================
// 룰 추가
// ============================================================

void CosmeticFilter::addRule(const CosmeticRule& rule) {
    // 예외 룰 처리
    if (rule.is_exception) {
        if (rule.domains.empty()) {
            global_exception_selectors_.insert(rule.css_selector);
        } else {
            for (const auto& domain : rule.domains) {
                exception_selectors_[domain].insert(rule.css_selector);
            }
        }
        return;
    }

    // 일반 vs 도메인 특정
    if (rule.domains.empty()) {
        generic_rules_.push_back(rule);
    } else {
        for (const auto& domain : rule.domains) {
            domain_rules_[domain].push_back(rule);
        }
    }
}

// ============================================================
// CSS 스타일시트 생성
// ============================================================

std::string CosmeticFilter::generateStylesheet(const std::string& domain) const {
    auto selectors = getSelectorsForDomain(domain);

    if (selectors.empty()) {
        return {};
    }

    // 셀렉터를 결합하여 { display: none !important; } 스타일 생성
    // 한 번에 최대 4096개씩 묶기 (브라우저 셀렉터 리스트 제한)
    std::ostringstream css;
    constexpr size_t BATCH_SIZE = 4096;

    for (size_t i = 0; i < selectors.size(); i += BATCH_SIZE) {
        size_t end = std::min(i + BATCH_SIZE, selectors.size());
        for (size_t j = i; j < end; ++j) {
            if (j > i) css << ",\n";
            css << selectors[j];
        }
        css << " {\n  display: none !important;\n}\n\n";
    }

    return css.str();
}

// ============================================================
// 도메인별 셀렉터 목록
// ============================================================

std::vector<std::string> CosmeticFilter::getSelectorsForDomain(
    const std::string& domain) const {

    std::string lower_domain = toLower(domain);
    std::string registrable = getRegistrableDomain(lower_domain);

    std::vector<std::string> selectors;
    selectors.reserve(generic_rules_.size() + 100);

    // 1. 일반(generic) 룰 추가
    for (const auto& rule : generic_rules_) {
        if (rule.is_extended || rule.is_script_inject) continue;

        // 제외 도메인 확인
        bool excluded = false;
        for (const auto& ed : rule.exclude_domains) {
            if (lower_domain == ed || lower_domain.ends_with("." + ed) ||
                registrable == ed) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;

        // 예외 확인
        if (!isExcepted(rule.css_selector, lower_domain)) {
            selectors.push_back(rule.css_selector);
        }
    }

    // 2. 도메인 특정 룰 추가
    auto addDomainRules = [&](const std::string& key) {
        auto it = domain_rules_.find(key);
        if (it == domain_rules_.end()) return;

        for (const auto& rule : it->second) {
            if (rule.is_extended || rule.is_script_inject) continue;

            if (!isExcepted(rule.css_selector, lower_domain)) {
                selectors.push_back(rule.css_selector);
            }
        }
    };

    // 정확한 도메인 매칭
    addDomainRules(lower_domain);

    // 등록 가능 도메인 매칭
    if (registrable != lower_domain) {
        addDomainRules(registrable);
    }

    // 서브도메인 부모 매칭 (예: sub.example.com → example.com)
    std::string parent = lower_domain;
    while (true) {
        auto dot_pos = parent.find('.');
        if (dot_pos == std::string::npos) break;
        parent = parent.substr(dot_pos + 1);
        if (parent.find('.') == std::string::npos) break; // TLD만 남으면 중지
        addDomainRules(parent);
    }

    // 중복 제거
    std::sort(selectors.begin(), selectors.end());
    selectors.erase(
        std::unique(selectors.begin(), selectors.end()),
        selectors.end());

    return selectors;
}

// ============================================================
// 확장 CSS 룰 목록
// ============================================================

std::vector<CosmeticRule> CosmeticFilter::getExtendedRulesForDomain(
    const std::string& domain) const {

    std::string lower_domain = toLower(domain);
    std::vector<CosmeticRule> rules;

    // 일반 확장 룰
    for (const auto& rule : generic_rules_) {
        if (!rule.is_extended) continue;
        if (!isExcepted(rule.css_selector, lower_domain)) {
            rules.push_back(rule);
        }
    }

    // 도메인 특정 확장 룰
    auto addExtended = [&](const std::string& key) {
        auto it = domain_rules_.find(key);
        if (it == domain_rules_.end()) return;
        for (const auto& rule : it->second) {
            if (!rule.is_extended) continue;
            if (!isExcepted(rule.css_selector, lower_domain)) {
                rules.push_back(rule);
            }
        }
    };

    addExtended(lower_domain);
    addExtended(getRegistrableDomain(lower_domain));

    return rules;
}

// ============================================================
// 통계
// ============================================================

int CosmeticFilter::totalRuleCount() const {
    int count = static_cast<int>(generic_rules_.size());
    for (const auto& [_, rules] : domain_rules_) {
        count += static_cast<int>(rules.size());
    }
    return count;
}

int CosmeticFilter::genericRuleCount() const {
    return static_cast<int>(generic_rules_.size());
}

int CosmeticFilter::domainSpecificRuleCount() const {
    int count = 0;
    for (const auto& [_, rules] : domain_rules_) {
        count += static_cast<int>(rules.size());
    }
    return count;
}

void CosmeticFilter::clear() {
    generic_rules_.clear();
    domain_rules_.clear();
    exception_selectors_.clear();
    global_exception_selectors_.clear();
}

// ============================================================
// Private 메서드
// ============================================================

bool CosmeticFilter::domainMatches(
    const std::string& page_domain,
    const std::vector<std::string>& rule_domains) {

    for (const auto& rd : rule_domains) {
        if (page_domain == rd ||
            page_domain.ends_with("." + rd)) {
            return true;
        }
    }
    return false;
}

bool CosmeticFilter::isExcepted(
    const std::string& selector,
    const std::string& domain) const {

    // 전역 예외
    if (global_exception_selectors_.count(selector)) {
        return true;
    }

    // 도메인별 예외
    auto it = exception_selectors_.find(domain);
    if (it != exception_selectors_.end() && it->second.count(selector)) {
        return true;
    }

    // 등록 가능 도메인 예외
    std::string registrable = getRegistrableDomain(domain);
    if (registrable != domain) {
        it = exception_selectors_.find(registrable);
        if (it != exception_selectors_.end() && it->second.count(selector)) {
            return true;
        }
    }

    return false;
}

bool CosmeticFilter::isExtendedCSS(const std::string& selector) {
    // :has(), :has-text(), :not(), :matches-css() 등
    return selector.find(":has(") != std::string::npos ||
           selector.find(":has-text(") != std::string::npos ||
           selector.find(":matches-css(") != std::string::npos ||
           selector.find(":xpath(") != std::string::npos ||
           selector.find(":style(") != std::string::npos ||
           selector.find(":remove()") != std::string::npos ||
           selector.find(":-abp-has(") != std::string::npos ||
           selector.find(":-abp-contains(") != std::string::npos;
}

std::string CosmeticFilter::getRegistrableDomain(const std::string& domain) {
    return simpleRegistrableDomain(domain);
}

} // namespace ordinal::adblock
