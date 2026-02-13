/**
 * @file filter_parser.cpp
 * @brief ABP/uBlock 필터 구문 파서 구현
 *
 * || 앵커, | 시작/끝 앵커, ^ 구분자, * 와일드카드 변환,
 * 옵션 파싱 ($script,$image,$third-party,$domain=...),
 * 정규식 룰 컴파일, 도메인 인덱스 구축.
 */

#include "filter_parser.h"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace ordinal::adblock {

// ============================================================
// ResourceType 변환
// ============================================================

ResourceType resourceTypeFromString(const std::string& str) {
    // 소문자 변환
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    if (s == "script")              return ResourceType::Script;
    if (s == "image" || s == "img") return ResourceType::Image;
    if (s == "stylesheet" || s == "css") return ResourceType::Stylesheet;
    if (s == "object")              return ResourceType::Object;
    if (s == "xmlhttprequest" || s == "xhr") return ResourceType::XmlHttp;
    if (s == "subdocument" || s == "frame") return ResourceType::SubDocument;
    if (s == "font")                return ResourceType::Font;
    if (s == "media")               return ResourceType::Media;
    if (s == "websocket")           return ResourceType::WebSocket;
    if (s == "popup")               return ResourceType::Popup;
    if (s == "document" || s == "doc") return ResourceType::Document;
    if (s == "other")               return ResourceType::Other;

    return ResourceType::None;
}

// ============================================================
// 생성자 / 소멸자
// ============================================================

FilterParser::FilterParser() = default;
FilterParser::~FilterParser() = default;

// ============================================================
// 필터 리스트 전체 파싱
// ============================================================

std::vector<NetworkRule> FilterParser::parseFilterList(
    const std::string& text,
    const std::string& source_name) const {

    std::vector<NetworkRule> rules;

    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        // 줄 끝 공백/CR 제거
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        // 앞 공백 제거
        size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos) {
            line = line.substr(start);
        }

        if (line.empty()) continue;

        // 주석/메타데이터 건너뛰기
        if (isComment(line)) continue;

        // 코스메틱 필터는 여기서 건너뛰기 (CosmeticFilter에서 처리)
        if (isCosmeticFilter(line)) continue;

        auto rule = parseLine(line);
        if (rule) {
            rule->source_list = source_name;
            rules.push_back(std::move(*rule));
        }
    }

    return rules;
}

// ============================================================
// 단일 필터 라인 파싱
// ============================================================

std::optional<NetworkRule> FilterParser::parseLine(const std::string& line) const {
    if (line.empty() || isComment(line) || isCosmeticFilter(line)) {
        return std::nullopt;
    }

    NetworkRule rule;
    rule.id = next_id_++;
    rule.raw_text = line;

    std::string filter = line;

    // 예외 룰 확인 (@@)
    if (filter.starts_with("@@")) {
        rule.is_exception = true;
        filter = filter.substr(2);
    }

    // 옵션 분리 ($... 부분)
    // $ 찾기 — 정규식 내부의 $는 제외
    std::string pattern_part = filter;
    std::string options_part;

    // 마지막 $를 찾되, 정규식 패턴 내부가 아닌지 확인
    if (!filter.starts_with("/") || !filter.ends_with("/")) {
        // 비정규식 — $ 구분자로 옵션 분리
        auto dollar_pos = filter.rfind('$');
        if (dollar_pos != std::string::npos && dollar_pos > 0) {
            pattern_part = filter.substr(0, dollar_pos);
            options_part = filter.substr(dollar_pos + 1);
        }
    }

    // 옵션 파싱
    if (!options_part.empty()) {
        parseOptions(options_part, rule);
    }

    // 정규식 패턴 확인 (/regex/)
    if (pattern_part.size() >= 2 &&
        pattern_part.front() == '/' && pattern_part.back() == '/') {
        // 순수 정규식
        std::string regex_str = pattern_part.substr(1, pattern_part.size() - 2);
        rule.pattern = regex_str;
        rule.has_regex = true;
        try {
            rule.compiled_regex = std::regex(regex_str,
                std::regex::ECMAScript | std::regex::optimize | std::regex::icase);
        } catch (const std::regex_error& e) {
            std::cerr << "[FilterParser] 정규식 컴파일 실패: " << regex_str
                      << " — " << e.what() << std::endl;
            return std::nullopt;
        }
        return rule;
    }

    // ABP 패턴 파싱

    // || 도메인 앵커
    if (pattern_part.starts_with("||")) {
        rule.anchor_domain = true;
        pattern_part = pattern_part.substr(2);
    }
    // | 시작 앵커
    else if (pattern_part.starts_with("|")) {
        rule.anchor_start = true;
        pattern_part = pattern_part.substr(1);
    }

    // | 끝 앵커
    if (pattern_part.ends_with("|")) {
        rule.anchor_end = true;
        pattern_part.pop_back();
    }

    // 패턴을 정규식으로 변환
    std::string regex_str = patternToRegex(pattern_part);

    // 앵커 적용
    if (rule.anchor_domain) {
        // || → (https?://)?([a-z0-9-]+\\.)*domain
        regex_str = "^(https?://)?(www\\.)?([a-z0-9-]+\\.)*" + regex_str;
    } else if (rule.anchor_start) {
        regex_str = "^" + regex_str;
    }

    if (rule.anchor_end) {
        regex_str = regex_str + "$";
    }

    rule.pattern = regex_str;
    rule.has_regex = true;

    try {
        rule.compiled_regex = std::regex(regex_str,
            std::regex::ECMAScript | std::regex::optimize | std::regex::icase);
    } catch (const std::regex_error& e) {
        // 컴파일 실패 시 단순 문자열 매칭으로 폴백
        rule.has_regex = false;
        rule.pattern = pattern_part;
    }

    return rule;
}

// ============================================================
// 메타데이터 추출
// ============================================================

FilterListMeta FilterParser::parseMetadata(const std::string& text) const {
    FilterListMeta meta;

    std::istringstream stream(text);
    std::string line;
    int line_count = 0;
    int rule_count = 0;

    while (std::getline(stream, line)) {
        line_count++;

        // 처음 50줄에서만 메타데이터 검색
        if (line_count <= 50) {
            // ! Title: ...
            if (line.starts_with("! Title:")) {
                meta.title = line.substr(8);
                // 앞뒤 공백 제거
                auto s = meta.title.find_first_not_of(" ");
                if (s != std::string::npos) meta.title = meta.title.substr(s);
            }
            // ! Homepage: ...
            else if (line.starts_with("! Homepage:")) {
                meta.homepage = line.substr(11);
                auto s = meta.homepage.find_first_not_of(" ");
                if (s != std::string::npos) meta.homepage = meta.homepage.substr(s);
            }
            // ! Version: ...
            else if (line.starts_with("! Version:")) {
                meta.version = line.substr(10);
                auto s = meta.version.find_first_not_of(" ");
                if (s != std::string::npos) meta.version = meta.version.substr(s);
            }
            // ! Expires: N hours / N days
            else if (line.starts_with("! Expires:")) {
                std::string expires = line.substr(10);
                auto s = expires.find_first_not_of(" ");
                if (s != std::string::npos) expires = expires.substr(s);

                // "4 days" → 96시간, "12 hours" → 12시간
                int num = 0;
                try { num = std::stoi(expires); } catch (...) {}
                if (expires.find("day") != std::string::npos) {
                    meta.expires_hours = num * 24;
                } else {
                    meta.expires_hours = num;
                }
            }
        }

        // 룰 카운트
        if (!line.empty() && !isComment(line)) {
            rule_count++;
        }
    }

    meta.rule_count = rule_count;
    return meta;
}

// ============================================================
// 유틸리티
// ============================================================

bool FilterParser::isCosmeticFilter(const std::string& line) {
    // ## 또는 #@# 또는 #?# (확장 CSS)
    return line.find("##") != std::string::npos ||
           line.find("#@#") != std::string::npos ||
           line.find("#?#") != std::string::npos;
}

bool FilterParser::isComment(const std::string& line) {
    if (line.empty()) return true;
    if (line[0] == '!' || line[0] == '#') return true;
    if (line.starts_with("[Adblock")) return true;

    return false;
}

// ============================================================
// 도메인 인덱스 구축
// ============================================================

std::unordered_map<std::string, std::vector<size_t>> FilterParser::buildDomainIndex(
    const std::vector<NetworkRule>& rules) {

    std::unordered_map<std::string, std::vector<size_t>> index;

    for (size_t i = 0; i < rules.size(); ++i) {
        const auto& rule = rules[i];

        if (!rule.include_domains.empty()) {
            // 특정 도메인 전용 룰
            for (const auto& domain : rule.include_domains) {
                index[domain].push_back(i);
            }
        } else if (rule.anchor_domain && !rule.pattern.empty()) {
            // || 앵커 룰에서 도메인 추출 시도
            // 단순화: 전체(generic) 룰로 분류
            index["*"].push_back(i);
        } else {
            // 전체(generic) 룰
            index["*"].push_back(i);
        }
    }

    return index;
}

// ============================================================
// Private: 패턴 → 정규식 변환
// ============================================================

std::string FilterParser::patternToRegex(const std::string& pattern) {
    std::string regex;
    regex.reserve(pattern.size() * 2);

    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];
        switch (c) {
            case '*':
                // * → .*  (와일드카드)
                regex += ".*";
                break;
            case '^':
                // ^ → 구분자 (알파벳/숫자/_/%-가 아닌 문자 또는 문자열 끝)
                regex += "([^a-zA-Z0-9_.%-]|$)";
                break;
            case '|':
                // 이미 앵커 처리됨 — 패턴 내부의 |은 리터럴
                regex += "\\|";
                break;
            // 정규식 메타문자 이스케이프
            case '.': regex += "\\."; break;
            case '+': regex += "\\+"; break;
            case '?': regex += "\\?"; break;
            case '{': regex += "\\{"; break;
            case '}': regex += "\\}"; break;
            case '[': regex += "\\["; break;
            case ']': regex += "\\]"; break;
            case '(': regex += "\\("; break;
            case ')': regex += "\\)"; break;
            case '\\': regex += "\\\\"; break;
            case '/': regex += "\\/"; break;
            case '$': regex += "\\$"; break;
            default:
                regex += c;
                break;
        }
    }

    return regex;
}

// ============================================================
// Private: 옵션 파싱
// ============================================================

void FilterParser::parseOptions(const std::string& options_str, NetworkRule& rule) const {
    // 콤마로 분리
    std::vector<std::string> options;
    std::istringstream iss(options_str);
    std::string opt;
    while (std::getline(iss, opt, ',')) {
        // 공백 제거
        auto s = opt.find_first_not_of(" ");
        if (s != std::string::npos) opt = opt.substr(s);
        auto e = opt.find_last_not_of(" ");
        if (e != std::string::npos) opt = opt.substr(0, e + 1);
        if (!opt.empty()) options.push_back(opt);
    }

    bool has_type_option = false;
    ResourceType allowed_types = ResourceType::None;

    for (const auto& o : options) {
        std::string opt_lower = o;
        std::transform(opt_lower.begin(), opt_lower.end(), opt_lower.begin(), ::tolower);

        // 부정 옵션 (~)
        bool negated = opt_lower.starts_with("~");
        std::string opt_name = negated ? opt_lower.substr(1) : opt_lower;

        // third-party / ~third-party
        if (opt_name == "third-party" || opt_name == "3p") {
            if (negated) {
                rule.first_party = true;
            } else {
                rule.third_party = true;
            }
            continue;
        }
        if (opt_name == "first-party" || opt_name == "1p") {
            if (negated) {
                rule.third_party = true;
            } else {
                rule.first_party = true;
            }
            continue;
        }

        // domain=...
        if (opt_name.starts_with("domain=")) {
            parseDomainOption(opt_name.substr(7), rule);
            continue;
        }

        // 리소스 타입
        ResourceType rt = resourceTypeFromString(opt_name);
        if (rt != ResourceType::None) {
            has_type_option = true;
            if (negated) {
                // 해당 타입 제외 — 모든 타입에서 빼기
                if (allowed_types == ResourceType::None) {
                    allowed_types = ResourceType::All;
                }
                allowed_types = allowed_types & ~rt;
            } else {
                allowed_types = allowed_types | rt;
            }
        }
    }

    if (has_type_option) {
        rule.resource_types = allowed_types;
    }
    // 타입 옵션이 없으면 All (기본값) 유지
}

// ============================================================
// Private: 도메인 옵션 파싱
// ============================================================

void FilterParser::parseDomainOption(const std::string& domain_str, NetworkRule& rule) {
    // | 구분자로 분리
    std::istringstream iss(domain_str);
    std::string domain;
    while (std::getline(iss, domain, '|')) {
        if (domain.empty()) continue;

        if (domain.starts_with("~")) {
            // 제외 도메인
            rule.exclude_domains.push_back(domain.substr(1));
        } else {
            // 포함 도메인
            rule.include_domains.push_back(domain);
        }
    }
}

} // namespace ordinal::adblock
