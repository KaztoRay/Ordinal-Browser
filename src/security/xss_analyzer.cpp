/**
 * @file xss_analyzer.cpp
 * @brief XSS (크로스 사이트 스크립팅) 분석기 구현
 * 
 * 다양한 XSS 공격 패턴을 감지하고 분류합니다.
 * HTML 인라인 이벤트, 스크립트 인젝션, DOM 기반 XSS 등을 탐지합니다.
 */

#include "xss_analyzer.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace ordinal::security {

// ============================================================
// 생성자 / 소멸자
// ============================================================

XssAnalyzer::XssAnalyzer() = default;
XssAnalyzer::~XssAnalyzer() = default;

// ============================================================
// 초기화
// ============================================================

bool XssAnalyzer::initialize(const XssAnalyzerConfig& config) {
    config_ = config;
    std::cout << "[XssAnalyzer] XSS 분석기 초기화 중..." << std::endl;

    // 스크립트 인젝션 패턴
    script_patterns_ = {
        {std::regex(R"(<script[^>]*>[^<]*</script>)", std::regex::icase),
         "인라인 스크립트 태그 감지"},
        {std::regex(R"(<script[^>]*src\s*=)", std::regex::icase),
         "외부 스크립트 로드 감지"},
        {std::regex(R"(<script[^>]*>.*?(eval|setTimeout|setInterval|Function)\s*\()", std::regex::icase),
         "동적 코드 실행 함수 감지"},
        {std::regex(R"(<!--.*?<script)", std::regex::icase),
         "HTML 주석 내 스크립트 인젝션"},
        {std::regex(R"(<img[^>]*src\s*=\s*["']?javascript:)", std::regex::icase),
         "이미지 태그 JavaScript 프로토콜"},
        {std::regex(R"(<svg[^>]*onload\s*=)", std::regex::icase),
         "SVG onload 이벤트 핸들러"},
        {std::regex(R"(<math[^>]*>.*?<annotation-xml[^>]*encoding\s*=\s*["']text/html["'])", std::regex::icase),
         "MathML 기반 XSS"},
        {std::regex(R"(<object[^>]*data\s*=\s*["']?javascript:)", std::regex::icase),
         "Object 태그 JavaScript 데이터"},
        {std::regex(R"(<embed[^>]*src\s*=\s*["']?javascript:)", std::regex::icase),
         "Embed 태그 JavaScript 소스"},
    };

    // 이벤트 핸들러 패턴 (on* 속성)
    handler_patterns_ = {
        {std::regex(R"(\bon(error|load|click|mouseover|mouseout|focus|blur|submit|change|input|keyup|keydown|keypress|dblclick|drag|drop|mouseenter|mouseleave)\s*=\s*["']?[^"']*?(alert|confirm|prompt|eval|document\.cookie|document\.location|window\.location|fetch|XMLHttpRequest))", std::regex::icase),
         "이벤트 핸들러에 위험한 함수 호출"},
        {std::regex(R"(\bon(error|load)\s*=\s*["'])", std::regex::icase),
         "onerror/onload 이벤트 핸들러 감지"},
        {std::regex(R"(\bonmouseover\s*=\s*["'].*?(alert|document\.cookie))", std::regex::icase),
         "마우스오버 이벤트 악용"},
        {std::regex(R"(\bonfocus\s*=\s*["'].*?autofocus)", std::regex::icase),
         "autofocus + onfocus 조합 XSS"},
    };

    // 위험한 HTML 속성 패턴
    attribute_patterns_ = {
        {std::regex(R"(href\s*=\s*["']?javascript\s*:)", std::regex::icase),
         "JavaScript 프로토콜 링크"},
        {std::regex(R"(href\s*=\s*["']?data\s*:text/html)", std::regex::icase),
         "data: URI HTML 주입"},
        {std::regex(R"(href\s*=\s*["']?vbscript\s*:)", std::regex::icase),
         "VBScript 프로토콜 링크"},
        {std::regex(R"(style\s*=\s*["'][^"']*expression\s*\()", std::regex::icase),
         "CSS expression() 사용"},
        {std::regex(R"(style\s*=\s*["'][^"']*url\s*\(\s*["']?javascript:)", std::regex::icase),
         "CSS url() 내 JavaScript"},
        {std::regex(R"(style\s*=\s*["'][^"']*-moz-binding\s*:)", std::regex::icase),
         "Mozilla XBL 바인딩"},
        {std::regex(R"(action\s*=\s*["']?javascript:)", std::regex::icase),
         "폼 액션에 JavaScript 프로토콜"},
        {std::regex(R"(formaction\s*=\s*["']?javascript:)", std::regex::icase),
         "폼 버튼 액션에 JavaScript"},
        {std::regex(R"(srcdoc\s*=\s*["'][^"']*<script)", std::regex::icase),
         "iframe srcdoc 내 스크립트"},
    };

    // DOM 싱크 패턴 (위험한 대입 대상)
    dom_sink_patterns_ = {
        {std::regex(R"(\b(document\.write|document\.writeln)\s*\()"),
         "document.write() 사용"},
        {std::regex(R"(\binnerHTML\s*=)"),
         "innerHTML 직접 대입"},
        {std::regex(R"(\bouterHTML\s*=)"),
         "outerHTML 직접 대입"},
        {std::regex(R"(\binsertAdjacentHTML\s*\()"),
         "insertAdjacentHTML() 사용"},
        {std::regex(R"(\b(eval|setTimeout|setInterval|Function)\s*\([^)]*\+)"),
         "동적 코드 실행에 문자열 연결"},
        {std::regex(R"(\bdocument\.location\s*=)"),
         "document.location 직접 설정"},
        {std::regex(R"(\bwindow\.location\s*=)"),
         "window.location 직접 설정"},
        {std::regex(R"(\blocation\.href\s*=)"),
         "location.href 직접 설정"},
        {std::regex(R"(\b\.setAttribute\s*\(\s*["'](on\w+|href|src|action)["'])"),
         "setAttribute로 위험한 속성 설정"},
        {std::regex(R"(\bjQuery\s*\(\s*["']<)"),
         "jQuery HTML 문자열 전달"},
        {std::regex(R"(\b\$\s*\(\s*["']<)"),
         "jQuery $ 함수에 HTML 전달"},
    };

    // DOM 소스 패턴 (사용자 입력 원본)
    dom_source_patterns_ = {
        {std::regex(R"(\b(document\.URL|document\.documentURI)\b)"),
         "document.URL 참조"},
        {std::regex(R"(\bdocument\.referrer\b)"),
         "document.referrer 참조"},
        {std::regex(R"(\blocation\.(href|search|hash|pathname)\b)"),
         "location 속성 참조"},
        {std::regex(R"(\bwindow\.name\b)"),
         "window.name 참조"},
        {std::regex(R"(\bdocument\.cookie\b)"),
         "document.cookie 참조"},
        {std::regex(R"(\b(localStorage|sessionStorage)\.(getItem|key)\b)"),
         "Web Storage 참조"},
        {std::regex(R"(\bpostMessage\b)"),
         "postMessage 사용"},
    };

    std::cout << "[XssAnalyzer] ✓ 스크립트 패턴 " << script_patterns_.size() << "개" << std::endl;
    std::cout << "[XssAnalyzer] ✓ 핸들러 패턴 " << handler_patterns_.size() << "개" << std::endl;
    std::cout << "[XssAnalyzer] ✓ 속성 패턴 " << attribute_patterns_.size() << "개" << std::endl;
    std::cout << "[XssAnalyzer] ✓ DOM 싱크/소스 패턴 " 
              << dom_sink_patterns_.size() + dom_source_patterns_.size() << "개" << std::endl;
    return true;
}

// ============================================================
// HTML 분석
// ============================================================

std::vector<ThreatReport> XssAnalyzer::analyzeHtml(
    const std::string& url,
    const std::string& html
) {
    std::vector<ThreatReport> threats;

    if (html.empty()) return threats;

    // 1. 스크립트 인젝션 검사
    auto script_detections = checkScriptInjection(html);

    // 2. 인라인 이벤트 핸들러 검사
    auto handler_detections = checkInlineHandlers(html);

    // 3. 위험한 속성 검사
    auto attr_detections = checkDangerousAttributes(html);

    // 4. 반사형 XSS 검사 (URL 파라미터가 HTML에 반영되는지)
    if (config_.detect_reflected) {
        auto params = parseQueryParams(url);
        for (const auto& [key, value] : params) {
            if (value.length() < 3) continue;  // 짧은 값은 무시

            // URL 파라미터 값이 HTML에 직접 포함되어 있는지 확인
            if (html.find(value) != std::string::npos) {
                double risk = evaluateInputRisk(value);
                if (risk >= config_.confidence_threshold) {
                    XssDetection det;
                    det.type = XssType::Reflected;
                    det.payload = value;
                    det.context = "URL 파라미터 '" + key + "'가 HTML에 반영됨";
                    det.confidence = risk;
                    det.sanitization_advice = "출력 시 HTML 엔티티 인코딩 필요";

                    // 위협 보고서 생성
                    ThreatReport report;
                    report.category = ThreatCategory::XSS;
                    report.severity = (risk >= 0.8) ? ThreatSeverity::High : ThreatSeverity::Medium;
                    report.url = url;
                    report.description = "반사형 XSS 가능성: 파라미터 '" + key + 
                                        "'의 값이 페이지에 필터링 없이 반영됩니다.";
                    report.recommendation = "입력값을 HTML 엔티티로 이스케이프하세요.";
                    report.detector_name = "XssAnalyzer/Reflected";
                    report.confidence = risk;
                    report.detected_at = std::chrono::system_clock::now();
                    report.metadata["param_name"] = key;
                    report.metadata["param_value"] = value;
                    threats.push_back(report);
                }
            }
        }
    }

    // 탐지 결과를 ThreatReport로 변환
    auto convert_detections = [&](const std::vector<XssDetection>& detections, 
                                   const std::string& sub_detector) {
        for (const auto& det : detections) {
            if (det.confidence < config_.confidence_threshold) continue;

            ThreatReport report;
            report.category = ThreatCategory::XSS;
            report.url = url;
            report.detector_name = "XssAnalyzer/" + sub_detector;
            report.confidence = det.confidence;
            report.detected_at = std::chrono::system_clock::now();
            report.metadata["payload"] = det.payload;
            report.metadata["context"] = det.context;

            if (det.confidence >= 0.85) {
                report.severity = ThreatSeverity::High;
            } else if (det.confidence >= 0.6) {
                report.severity = ThreatSeverity::Medium;
            } else {
                report.severity = ThreatSeverity::Low;
            }

            report.description = det.context;
            report.recommendation = det.sanitization_advice.empty() ? 
                "위험한 패턴을 제거하거나 이스케이프하세요." : det.sanitization_advice;

            threats.push_back(report);
        }
    };

    convert_detections(script_detections, "ScriptInjection");
    convert_detections(handler_detections, "EventHandler");
    convert_detections(attr_detections, "DangerousAttr");

    return threats;
}

// ============================================================
// 스크립트 분석
// ============================================================

std::vector<ThreatReport> XssAnalyzer::analyzeScript(
    const std::string& url,
    const std::string& script
) {
    std::vector<ThreatReport> threats;

    if (!config_.detect_dom_based || script.empty()) return threats;

    auto dom_detections = checkDomManipulation(script);
    auto source_sink_detections = checkSourceSinkPatterns(script);

    auto convert = [&](const std::vector<XssDetection>& dets, const std::string& name) {
        for (const auto& det : dets) {
            if (det.confidence < config_.confidence_threshold) continue;

            ThreatReport report;
            report.category = ThreatCategory::XSS;
            report.severity = (det.confidence >= 0.8) ? ThreatSeverity::High : ThreatSeverity::Medium;
            report.url = url;
            report.description = "DOM 기반 XSS: " + det.context;
            report.recommendation = "안전한 DOM API를 사용하세요 (textContent, setAttribute 등).";
            report.detector_name = "XssAnalyzer/" + name;
            report.confidence = det.confidence;
            report.detected_at = std::chrono::system_clock::now();
            report.metadata["payload"] = det.payload;
            threats.push_back(report);
        }
    };

    convert(dom_detections, "DomManipulation");
    convert(source_sink_detections, "SourceSink");

    return threats;
}

// ============================================================
// URL 파라미터 검사
// ============================================================

std::vector<std::pair<std::string, std::string>> XssAnalyzer::checkUrlParams(
    const std::string& url
) {
    std::vector<std::pair<std::string, std::string>> dangerous_params;
    auto params = parseQueryParams(url);

    for (const auto& [key, value] : params) {
        double risk = evaluateInputRisk(value);
        if (risk >= 0.4) {
            dangerous_params.emplace_back(key, value);
        }
    }

    return dangerous_params;
}

// ============================================================
// 입력 위험도 평가
// ============================================================

double XssAnalyzer::evaluateInputRisk(const std::string& input) {
    double risk = 0.0;
    std::string decoded = decodePayload(input);
    std::string lower = decoded;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // HTML 태그 포함 여부
    if (lower.find('<') != std::string::npos && lower.find('>') != std::string::npos) {
        risk += 0.3;
    }

    // script 태그
    if (lower.find("<script") != std::string::npos) {
        risk += 0.5;
    }

    // 이벤트 핸들러
    std::regex event_re(R"(\bon\w+\s*=)", std::regex::icase);
    if (std::regex_search(lower, event_re)) {
        risk += 0.4;
    }

    // JavaScript 프로토콜
    if (lower.find("javascript:") != std::string::npos) {
        risk += 0.5;
    }

    // 위험한 함수 호출
    std::vector<std::string> dangerous_funcs = {
        "alert(", "eval(", "document.cookie", "document.write(",
        "prompt(", "confirm(", "fetch(", "xmlhttprequest"
    };
    for (const auto& func : dangerous_funcs) {
        if (lower.find(func) != std::string::npos) {
            risk += 0.3;
            break;
        }
    }

    // 인코딩 우회 시도
    if (lower.find("&#") != std::string::npos || 
        lower.find("\\x") != std::string::npos ||
        lower.find("\\u") != std::string::npos) {
        risk += 0.2;
    }

    return std::min(risk, 1.0);
}

// ============================================================
// 상세 스캔
// ============================================================

std::vector<XssDetection> XssAnalyzer::detailedScan(
    const std::string& url,
    const std::string& html,
    const std::string& script
) {
    std::vector<XssDetection> all_detections;

    auto scripts = checkScriptInjection(html);
    auto handlers = checkInlineHandlers(html);
    auto attrs = checkDangerousAttributes(html);

    all_detections.insert(all_detections.end(), scripts.begin(), scripts.end());
    all_detections.insert(all_detections.end(), handlers.begin(), handlers.end());
    all_detections.insert(all_detections.end(), attrs.begin(), attrs.end());

    if (!script.empty()) {
        auto dom = checkDomManipulation(script);
        auto sources = checkSourceSinkPatterns(script);
        all_detections.insert(all_detections.end(), dom.begin(), dom.end());
        all_detections.insert(all_detections.end(), sources.begin(), sources.end());
    }

    // 반사형 XSS 검사
    auto params = parseQueryParams(url);
    for (const auto& [key, value] : params) {
        if (value.length() >= 3 && html.find(value) != std::string::npos) {
            double risk = evaluateInputRisk(value);
            if (risk >= config_.confidence_threshold) {
                XssDetection det;
                det.type = XssType::Reflected;
                det.payload = value;
                det.context = "URL 파라미터 '" + key + "'가 HTML에 반영됨";
                det.confidence = risk;
                det.sanitization_advice = "서버 측에서 출력 인코딩 적용 필요";
                all_detections.push_back(det);
            }
        }
    }

    // 신뢰도 기준으로 정렬 (높은 순)
    std::sort(all_detections.begin(), all_detections.end(),
              [](const XssDetection& a, const XssDetection& b) {
                  return a.confidence > b.confidence;
              });

    return all_detections;
}

// ============================================================
// XSS 무력화 (이스케이프)
// ============================================================

std::string XssAnalyzer::sanitize(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 1.2);  // 여유 공간 확보

    for (char c : input) {
        switch (c) {
            case '<':  output += "&lt;";    break;
            case '>':  output += "&gt;";    break;
            case '&':  output += "&amp;";   break;
            case '"':  output += "&quot;";  break;
            case '\'': output += "&#x27;";  break;
            case '/':  output += "&#x2F;";  break;
            case '`':  output += "&#96;";   break;
            default:   output += c;         break;
        }
    }

    return output;
}

// ============================================================
// 내부 검사 메서드
// ============================================================

std::vector<XssDetection> XssAnalyzer::checkScriptInjection(const std::string& html) {
    std::vector<XssDetection> detections;

    for (const auto& [pattern, description] : script_patterns_) {
        std::sregex_iterator it(html.begin(), html.end(), pattern);
        std::sregex_iterator end;

        while (it != end) {
            XssDetection det;
            det.type = XssType::Stored;
            det.payload = it->str();
            det.context = description;
            det.confidence = 0.75;
            det.sanitization_advice = "스크립트 태그 및 JavaScript 프로토콜을 필터링하세요.";

            // 추가 확신도 조정
            std::string match_lower = det.payload;
            std::transform(match_lower.begin(), match_lower.end(), match_lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            if (match_lower.find("alert") != std::string::npos ||
                match_lower.find("eval") != std::string::npos ||
                match_lower.find("document.cookie") != std::string::npos) {
                det.confidence = 0.95;
            }

            // 라인 번호 계산
            auto pos = it->position();
            det.line_number = static_cast<int>(
                std::count(html.begin(), html.begin() + pos, '\n') + 1
            );

            detections.push_back(det);
            ++it;
        }
    }

    return detections;
}

std::vector<XssDetection> XssAnalyzer::checkInlineHandlers(const std::string& html) {
    std::vector<XssDetection> detections;

    for (const auto& [pattern, description] : handler_patterns_) {
        std::sregex_iterator it(html.begin(), html.end(), pattern);
        std::sregex_iterator end;

        while (it != end) {
            XssDetection det;
            det.type = XssType::Stored;
            det.payload = it->str();
            det.context = description;
            det.confidence = 0.8;
            det.sanitization_advice = "인라인 이벤트 핸들러 대신 addEventListener를 사용하세요.";

            auto pos = it->position();
            det.line_number = static_cast<int>(
                std::count(html.begin(), html.begin() + pos, '\n') + 1
            );

            detections.push_back(det);
            ++it;
        }
    }

    return detections;
}

std::vector<XssDetection> XssAnalyzer::checkDangerousAttributes(const std::string& html) {
    std::vector<XssDetection> detections;

    for (const auto& [pattern, description] : attribute_patterns_) {
        std::sregex_iterator it(html.begin(), html.end(), pattern);
        std::sregex_iterator end;

        while (it != end) {
            XssDetection det;
            det.type = XssType::Stored;
            det.payload = it->str();
            det.context = description;
            det.confidence = 0.85;
            det.sanitization_advice = "JavaScript 프로토콜과 위험한 CSS 표현식을 차단하세요.";

            auto pos = it->position();
            det.line_number = static_cast<int>(
                std::count(html.begin(), html.begin() + pos, '\n') + 1
            );

            detections.push_back(det);
            ++it;
        }
    }

    return detections;
}

std::vector<XssDetection> XssAnalyzer::checkDomManipulation(const std::string& script) {
    std::vector<XssDetection> detections;

    for (const auto& [pattern, description] : dom_sink_patterns_) {
        std::sregex_iterator it(script.begin(), script.end(), pattern);
        std::sregex_iterator end;

        while (it != end) {
            XssDetection det;
            det.type = XssType::DomBased;
            det.payload = it->str();
            det.context = description;
            det.confidence = 0.7;
            det.sanitization_advice = "textContent나 createElement를 대신 사용하세요.";

            auto pos = it->position();
            det.line_number = static_cast<int>(
                std::count(script.begin(), script.begin() + pos, '\n') + 1
            );

            detections.push_back(det);
            ++it;
        }
    }

    return detections;
}

std::vector<XssDetection> XssAnalyzer::checkSourceSinkPatterns(const std::string& script) {
    std::vector<XssDetection> detections;

    // 소스 패턴 감지
    std::vector<std::string> found_sources;
    for (const auto& [pattern, description] : dom_source_patterns_) {
        if (std::regex_search(script, pattern)) {
            found_sources.push_back(description);
        }
    }

    // 싱크 패턴 감지
    std::vector<std::string> found_sinks;
    for (const auto& [pattern, description] : dom_sink_patterns_) {
        if (std::regex_search(script, pattern)) {
            found_sinks.push_back(description);
        }
    }

    // 소스와 싱크가 모두 존재하면 DOM XSS 가능성 높음
    if (!found_sources.empty() && !found_sinks.empty()) {
        XssDetection det;
        det.type = XssType::DomBased;
        det.confidence = 0.8;
        det.context = "DOM 소스-싱크 패턴 감지: ";

        // 소스 목록
        det.context += "소스=[";
        for (size_t i = 0; i < found_sources.size(); ++i) {
            if (i > 0) det.context += ", ";
            det.context += found_sources[i];
        }
        det.context += "] → 싱크=[";

        // 싱크 목록
        for (size_t i = 0; i < found_sinks.size(); ++i) {
            if (i > 0) det.context += ", ";
            det.context += found_sinks[i];
        }
        det.context += "]";

        det.payload = "소스-싱크 연결";
        det.sanitization_advice = "사용자 입력이 DOM 싱크로 직접 전달되지 않도록 하세요.";

        detections.push_back(det);
    }

    return detections;
}

// ============================================================
// 유틸리티
// ============================================================

std::string XssAnalyzer::decodePayload(const std::string& encoded) const {
    std::string decoded = encoded;

    // URL 디코딩 (%XX → 문자)
    std::string url_decoded;
    url_decoded.reserve(decoded.size());
    for (size_t i = 0; i < decoded.size(); ++i) {
        if (decoded[i] == '%' && i + 2 < decoded.size()) {
            std::string hex = decoded.substr(i + 1, 2);
            try {
                char c = static_cast<char>(std::stoi(hex, nullptr, 16));
                url_decoded += c;
                i += 2;
            } catch (...) {
                url_decoded += decoded[i];
            }
        } else if (decoded[i] == '+') {
            url_decoded += ' ';
        } else {
            url_decoded += decoded[i];
        }
    }
    decoded = url_decoded;

    // HTML 엔티티 디코딩 (기본적인 것만)
    auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
    };

    replace_all(decoded, "&lt;", "<");
    replace_all(decoded, "&gt;", ">");
    replace_all(decoded, "&amp;", "&");
    replace_all(decoded, "&quot;", "\"");
    replace_all(decoded, "&#x27;", "'");
    replace_all(decoded, "&#39;", "'");
    replace_all(decoded, "&#x2F;", "/");
    replace_all(decoded, "&#47;", "/");

    return decoded;
}

std::unordered_map<std::string, std::string> XssAnalyzer::parseQueryParams(
    const std::string& url
) const {
    std::unordered_map<std::string, std::string> params;

    auto query_start = url.find('?');
    if (query_start == std::string::npos) return params;

    std::string query = url.substr(query_start + 1);

    // 프래그먼트 제거
    auto hash_pos = query.find('#');
    if (hash_pos != std::string::npos) {
        query = query.substr(0, hash_pos);
    }

    // & 기준으로 분리
    std::istringstream stream(query);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        auto eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + 1);
            params[key] = value;
        }
    }

    return params;
}

} // namespace ordinal::security
