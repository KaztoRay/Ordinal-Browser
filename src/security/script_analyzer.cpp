/**
 * @file script_analyzer.cpp
 * @brief JavaScript 스크립트 정적 분석기 구현
 * 
 * 악성 패턴 매칭, 난독화 감지, 엔트로피 분석,
 * 암호화폐 채굴/키로거/데이터탈취/스키머 탐지를 수행합니다.
 */

#include "script_analyzer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <sstream>
#include <array>

namespace ordinal::security {

// ============================================================
// 생성자 / 소멸자
// ============================================================

ScriptAnalyzer::ScriptAnalyzer() = default;
ScriptAnalyzer::~ScriptAnalyzer() = default;

// ============================================================
// 초기화
// ============================================================

bool ScriptAnalyzer::initialize(const ScriptAnalyzerConfig& config) {
    config_ = config;
    std::cout << "[ScriptAnalyzer] 스크립트 분석기 초기화 중..." << std::endl;

    // 암호화폐 채굴 패턴
    miner_patterns_ = {
        {std::regex(R"(\bCoinHive\b)", std::regex::icase),
         "CoinHive 암호화폐 채굴 라이브러리"},
        {std::regex(R"(\bcoinhive\.min\.js\b)", std::regex::icase),
         "CoinHive 스크립트 파일"},
        {std::regex(R"(\bCryptoNight\b)", std::regex::icase),
         "CryptoNight 해시 알고리즘 (모네로 채굴)"},
        {std::regex(R"(\bWebAssembly\.instantiate\b.*?(hash|mine|worker))"),
         "WebAssembly 기반 채굴 코드"},
        {std::regex(R"(\b(cryptonight|cn-gpu|cn-heavy|randomx)\b)", std::regex::icase),
         "채굴 알고리즘 식별자"},
        {std::regex(R"(\bnew\s+Worker\b.*?(mine|hash|crypto))", std::regex::icase),
         "Web Worker 기반 채굴"},
        {std::regex(R"(stratum\+tcp://|pool\.|mining\.)", std::regex::icase),
         "채굴 풀 주소"},
        {std::regex(R"(\bnavigator\.hardwareConcurrency\b)"),
         "CPU 코어 수 확인 (채굴 최적화)"},
        {std::regex(R"(\b(jsecoin|authedmine|coinimp|mineralt|webmine)\b)", std::regex::icase),
         "알려진 브라우저 채굴 서비스"},
    };

    // 키로거 패턴
    keylogger_patterns_ = {
        {std::regex(R"(\b(addEventListener|attachEvent)\s*\(\s*["'](keydown|keyup|keypress|input)["'])"),
         "키보드 이벤트 리스너 등록"},
        {std::regex(R"(\b(event\.key|event\.keyCode|event\.which|event\.charCode)\b)"),
         "키 이벤트 속성 접근"},
        {std::regex(R"(\bString\.fromCharCode\b.*?\b(send|post|fetch|ajax|XMLHttpRequest)\b)"),
         "키 입력을 문자로 변환 후 전송"},
        {std::regex(R"(\bdocument\.onkey(down|up|press)\s*=)"),
         "전역 키 이벤트 핸들러 설정"},
        {std::regex(R"(\b(textContent|innerText|value)\b.*?\b(push|concat|join)\b.*?\b(send|fetch|post)\b)"),
         "입력값 수집 후 전송 패턴"},
    };

    // 데이터 탈취 패턴
    stealer_patterns_ = {
        {std::regex(R"(\bdocument\.cookie\b.*?\b(send|fetch|post|ajax|XMLHttpRequest|Image\s*\()\b)"),
         "쿠키 탈취 후 전송"},
        {std::regex(R"(\b(localStorage|sessionStorage)\b.*?\b(send|fetch|post)\b)"),
         "웹 스토리지 데이터 탈취"},
        {std::regex(R"(\bnew\s+Image\(\)\.src\s*=.*?(cookie|token|session|password))"),
         "이미지 비콘을 이용한 데이터 유출"},
        {std::regex(R"(\bnavigator\.(userAgent|platform|language|plugins|mimeTypes)\b.*?(send|fetch|post))"),
         "브라우저 정보 수집 후 전송"},
        {std::regex(R"(\b(getComputedStyle|getBoundingClientRect|measureText)\b.*?\b(send|fetch)\b)"),
         "브라우저 핑거프린팅 후 전송"},
        {std::regex(R"(\bcanvas\b.*?\btoDataURL\b.*?\b(send|fetch|post)\b)"),
         "Canvas 핑거프린팅 후 전송"},
        {std::regex(R"(\b(AudioContext|OfflineAudioContext)\b.*?\b(send|fetch)\b)"),
         "오디오 핑거프린팅 후 전송"},
        {std::regex(R"(\bWebGL\b.*?\b(getParameter|getExtension)\b.*?\b(send|fetch)\b)"),
         "WebGL 핑거프린팅 후 전송"},
    };

    // 결제 스키머 패턴
    skimmer_patterns_ = {
        {std::regex(R"(\b(credit.?card|card.?number|cvv|cvc|expir|billing)\b)", std::regex::icase),
         "결제 관련 필드명 참조"},
        {std::regex(R"(\binput\[.*?(type=["']?(tel|number|text)["']?).*?\].*?(card|credit|payment))", std::regex::icase),
         "결제 입력 필드 셀렉터"},
        {std::regex(R"(\b(payment|checkout|billing|order)\b.*?\b(addEventListener|querySelector)\b)", std::regex::icase),
         "결제 폼 이벤트 감시"},
        {std::regex(R"(\bbtoa\b.*?\b(card|credit|cvv|payment)\b)", std::regex::icase),
         "결제 정보 Base64 인코딩"},
        {std::regex(R"(\b(Magecart|formjacking|skimmer)\b)", std::regex::icase),
         "알려진 스키머 시그니처"},
        {std::regex(R"(\bJSON\.stringify\b.*?\b(card|payment|billing)\b.*?\b(fetch|post|send)\b)", std::regex::icase),
         "결제 데이터 JSON 직렬화 후 전송"},
    };

    // 난독화 패턴
    obfuscation_patterns_ = {
        {std::regex(R"(eval\s*\(\s*(atob|unescape|decodeURIComponent|String\.fromCharCode)\s*\()"),
         "eval + 디코딩 함수 조합"},
        {std::regex(R"(\beval\s*\(\s*function\s*\()"),
         "eval에 함수를 직접 전달"},
        {std::regex(R"(\\x[0-9a-f]{2}){5,})", std::regex::icase),
         "대량 16진수 이스케이프"},
        {std::regex(R"((\\u[0-9a-f]{4}){5,})", std::regex::icase),
         "대량 유니코드 이스케이프"},
        {std::regex(R"(\bString\s*\[\s*["']fromCharCode["']\s*\])"),
         "문자열 인덱스 접근으로 함수 호출 (난독화)"},
        {std::regex(R"(\b_0x[a-f0-9]{4,}\b)"),
         "난독화 변수명 패턴 (_0x...)"},
        {std::regex(R"(\bvar\s+_\w+\s*=\s*\[["'][^"']+["'](\s*,\s*["'][^"']+["']){10,}\])"),
         "대형 문자열 배열 (난독화 룩업 테이블)"},
        {std::regex(R"(\bwindow\s*\[\s*["'][^"']+["']\s*\]\s*\[\s*["'][^"']+["']\s*\])"),
         "대괄호 표기법 중첩 (난독화)"},
        {std::regex(R"((function\s*\(\s*\w\s*,\s*\w\s*,\s*\w\s*\)\s*\{){2,})"),
         "단일 문자 매개변수 함수 중첩"},
    };

    // 의심스러운 API 목록
    suspicious_apis_ = {
        "eval(", "Function(", "setTimeout(", "setInterval(",
        "document.write(", "document.writeln(",
        "innerHTML", "outerHTML", "insertAdjacentHTML",
        "document.cookie", "localStorage", "sessionStorage",
        "XMLHttpRequest", "fetch(", "navigator.sendBeacon(",
        "WebSocket", "EventSource", "SharedWorker", "ServiceWorker",
        "crypto.subtle", "WebAssembly", "Proxy(", "Reflect.",
        "document.execCommand", "window.open(", "postMessage(",
        "requestAnimationFrame", "MutationObserver",
        "performance.now(", "Date.now(", "navigator.clipboard",
    };

    // 알려진 악성 스크립트 해시 (예시 - 실제로는 DB에서 로드)
    malware_hashes_ = {
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",  // 빈 해시 (테스트용)
    };

    std::cout << "[ScriptAnalyzer] ✓ 채굴 패턴 " << miner_patterns_.size() << "개" << std::endl;
    std::cout << "[ScriptAnalyzer] ✓ 키로거 패턴 " << keylogger_patterns_.size() << "개" << std::endl;
    std::cout << "[ScriptAnalyzer] ✓ 탈취 패턴 " << stealer_patterns_.size() << "개" << std::endl;
    std::cout << "[ScriptAnalyzer] ✓ 스키머 패턴 " << skimmer_patterns_.size() << "개" << std::endl;
    std::cout << "[ScriptAnalyzer] ✓ 난독화 패턴 " << obfuscation_patterns_.size() << "개" << std::endl;
    return true;
}

// ============================================================
// 스크립트 분석 (메인 인터페이스)
// ============================================================

std::vector<ThreatReport> ScriptAnalyzer::analyzeScript(
    const std::string& url,
    const std::string& script
) {
    std::vector<ThreatReport> threats;

    if (script.empty()) return threats;

    // 최대 크기 확인
    if (script.size() > config_.max_script_size) {
        std::cerr << "[ScriptAnalyzer] 스크립트 크기 초과: " << script.size() << " 바이트" << std::endl;
        return threats;
    }

    // 알려진 악성 해시 확인
    if (isKnownMalware(script)) {
        ThreatReport report;
        report.category = ThreatCategory::Malware;
        report.severity = ThreatSeverity::Critical;
        report.url = url;
        report.description = "알려진 악성 스크립트 해시가 일치합니다.";
        report.recommendation = "즉시 차단하세요.";
        report.detector_name = "ScriptAnalyzer/HashMatch";
        report.confidence = 1.0;
        report.detected_at = std::chrono::system_clock::now();
        threats.push_back(report);
        return threats;  // 확인된 악성코드이므로 추가 분석 불필요
    }

    // 상세 분석 수행
    auto result = detailedAnalysis(script);

    // 암호화폐 채굴 탐지
    if (config_.detect_crypto_miners) {
        double miner_score = detectCryptoMiner(script);
        if (miner_score >= config_.risk_threshold) {
            ThreatReport report;
            report.category = ThreatCategory::CryptoMining;
            report.severity = (miner_score >= 0.8) ? ThreatSeverity::High : ThreatSeverity::Medium;
            report.url = url;
            report.description = "암호화폐 채굴 스크립트가 감지되었습니다.";
            report.recommendation = "이 스크립트는 CPU 자원을 소모합니다. 차단을 권장합니다.";
            report.detector_name = "ScriptAnalyzer/CryptoMiner";
            report.confidence = miner_score;
            report.detected_at = std::chrono::system_clock::now();
            threats.push_back(report);
        }
    }

    // 키로거 탐지
    if (config_.detect_keyloggers) {
        double keylog_score = detectKeylogger(script);
        if (keylog_score >= config_.risk_threshold) {
            ThreatReport report;
            report.category = ThreatCategory::DataExfiltration;
            report.severity = (keylog_score >= 0.8) ? ThreatSeverity::Critical : ThreatSeverity::High;
            report.url = url;
            report.description = "키로거 패턴이 감지되었습니다.";
            report.recommendation = "키 입력이 외부로 전송될 수 있습니다. 즉시 차단하세요.";
            report.detector_name = "ScriptAnalyzer/Keylogger";
            report.confidence = keylog_score;
            report.detected_at = std::chrono::system_clock::now();
            threats.push_back(report);
        }
    }

    // 데이터 탈취 탐지
    if (config_.detect_data_stealers) {
        double stealer_score = detectDataStealer(script);
        if (stealer_score >= config_.risk_threshold) {
            ThreatReport report;
            report.category = ThreatCategory::DataExfiltration;
            report.severity = (stealer_score >= 0.8) ? ThreatSeverity::High : ThreatSeverity::Medium;
            report.url = url;
            report.description = "데이터 탈취 패턴이 감지되었습니다.";
            report.recommendation = "개인정보가 외부로 유출될 수 있습니다.";
            report.detector_name = "ScriptAnalyzer/DataStealer";
            report.confidence = stealer_score;
            report.detected_at = std::chrono::system_clock::now();
            threats.push_back(report);
        }
    }

    // 스키머 탐지
    if (config_.detect_skimmers) {
        double skimmer_score = detectSkimmer(script);
        if (skimmer_score >= config_.risk_threshold) {
            ThreatReport report;
            report.category = ThreatCategory::DataExfiltration;
            report.severity = ThreatSeverity::Critical;
            report.url = url;
            report.description = "결제 정보 스키밍 스크립트가 감지되었습니다.";
            report.recommendation = "결제 정보가 탈취될 수 있습니다. 즉시 이 페이지를 떠나세요.";
            report.detector_name = "ScriptAnalyzer/Skimmer";
            report.confidence = skimmer_score;
            report.detected_at = std::chrono::system_clock::now();
            threats.push_back(report);
        }
    }

    // 난독화 탐지
    if (config_.detect_obfuscation) {
        double obf_score = detectObfuscation(script);
        if (obf_score >= 0.7) {  // 난독화 자체는 더 높은 기준 적용
            ThreatReport report;
            report.category = ThreatCategory::SuspiciousScript;
            report.severity = (obf_score >= 0.9) ? ThreatSeverity::Medium : ThreatSeverity::Low;
            report.url = url;
            report.description = "고도로 난독화된 JavaScript 코드가 감지되었습니다.";
            report.recommendation = "난독화 자체가 악성은 아니지만, 의심스럽습니다.";
            report.detector_name = "ScriptAnalyzer/Obfuscation";
            report.confidence = obf_score;
            report.detected_at = std::chrono::system_clock::now();
            report.metadata["entropy"] = std::to_string(calculateEntropy(script));
            threats.push_back(report);
        }
    }

    return threats;
}

// ============================================================
// 상세 분석
// ============================================================

ScriptAnalysisResult ScriptAnalyzer::detailedAnalysis(const std::string& script) {
    ScriptAnalysisResult result;
    result.code_size = script.size();

    // 엔트로피 계산
    double entropy = calculateEntropy(script);
    result.entropy_score = static_cast<int>(entropy * 12.5);  // 0~100 스케일

    // 난독화 점수
    result.obfuscation_score = detectObfuscation(script);
    result.is_obfuscated = (result.obfuscation_score >= 0.6);

    // 의심 API 검색
    result.suspicious_apis = findSuspiciousApis(script);

    // 각 탐지기 점수 수집
    double miner = detectCryptoMiner(script);
    double keylog = detectKeylogger(script);
    double stealer = detectDataStealer(script);
    double skimmer = detectSkimmer(script);

    // 최대 위험 카테고리 결정
    double max_score = 0.0;
    MaliciousScriptType max_type = MaliciousScriptType::None;

    auto check = [&](double score, MaliciousScriptType type) {
        if (score > max_score) {
            max_score = score;
            max_type = type;
        }
    };

    check(miner, MaliciousScriptType::CryptoMiner);
    check(keylog, MaliciousScriptType::KeyLogger);
    check(stealer, MaliciousScriptType::DataStealer);
    check(skimmer, MaliciousScriptType::Skimmer);

    if (result.is_obfuscated && max_score < result.obfuscation_score) {
        max_type = MaliciousScriptType::Obfuscated;
        max_score = result.obfuscation_score;
    }

    result.type = max_type;
    result.risk_score = max_score;

    // 요약 생성
    std::ostringstream summary;
    summary << "코드 크기: " << result.code_size << " 바이트, ";
    summary << "엔트로피: " << entropy << ", ";
    summary << "난독화: " << (result.is_obfuscated ? "예" : "아니오") << ", ";
    summary << "의심 API: " << result.suspicious_apis.size() << "개, ";
    summary << "위험도: " << result.risk_score;
    result.summary = summary.str();

    // 지표 수집
    if (miner >= 0.4)   result.indicators.push_back("암호화폐 채굴 패턴 감지");
    if (keylog >= 0.4)  result.indicators.push_back("키로거 패턴 감지");
    if (stealer >= 0.4) result.indicators.push_back("데이터 탈취 패턴 감지");
    if (skimmer >= 0.4) result.indicators.push_back("결제 스키밍 패턴 감지");
    if (result.is_obfuscated) result.indicators.push_back("고도의 난독화");
    if (entropy > 6.5)  result.indicators.push_back("높은 엔트로피 (의심스러운 인코딩)");

    return result;
}

// ============================================================
// 암호화폐 채굴 탐지
// ============================================================

double ScriptAnalyzer::detectCryptoMiner(const std::string& script) {
    double score = 0.0;

    for (const auto& [pattern, desc] : miner_patterns_) {
        if (std::regex_search(script, pattern)) {
            score += 0.25;
        }
    }

    // 추가 휴리스틱: Web Worker + 무한 루프 + 해시 연산
    if (script.find("new Worker") != std::string::npos &&
        script.find("while") != std::string::npos &&
        (script.find("hash") != std::string::npos || 
         script.find("nonce") != std::string::npos)) {
        score += 0.3;
    }

    // SharedArrayBuffer + Atomics (고성능 채굴)
    if (script.find("SharedArrayBuffer") != std::string::npos &&
        script.find("Atomics") != std::string::npos) {
        score += 0.2;
    }

    return std::min(score, 1.0);
}

// ============================================================
// 키로거 탐지
// ============================================================

double ScriptAnalyzer::detectKeylogger(const std::string& script) {
    double score = 0.0;

    for (const auto& [pattern, desc] : keylogger_patterns_) {
        if (std::regex_search(script, pattern)) {
            score += 0.25;
        }
    }

    // 추가 휴리스틱: 키 이벤트 + 데이터 수집 + 네트워크 전송
    bool has_key_event = (script.find("keydown") != std::string::npos ||
                          script.find("keyup") != std::string::npos ||
                          script.find("keypress") != std::string::npos);
    bool has_collection = (script.find("push(") != std::string::npos ||
                           script.find("concat(") != std::string::npos ||
                           script.find("+=") != std::string::npos);
    bool has_send = (script.find("fetch(") != std::string::npos ||
                     script.find("XMLHttpRequest") != std::string::npos ||
                     script.find("sendBeacon") != std::string::npos ||
                     script.find("new Image") != std::string::npos);

    if (has_key_event && has_collection && has_send) {
        score += 0.4;
    }

    return std::min(score, 1.0);
}

// ============================================================
// 데이터 탈취 탐지
// ============================================================

double ScriptAnalyzer::detectDataStealer(const std::string& script) {
    double score = 0.0;

    for (const auto& [pattern, desc] : stealer_patterns_) {
        if (std::regex_search(script, pattern)) {
            score += 0.2;
        }
    }

    // 쿠키 + 외부 전송 조합
    if (script.find("document.cookie") != std::string::npos) {
        if (script.find("fetch(") != std::string::npos ||
            script.find("XMLHttpRequest") != std::string::npos ||
            script.find("new Image") != std::string::npos) {
            score += 0.3;
        }
    }

    // 대량 데이터 수집 패턴
    int data_access_count = 0;
    std::vector<std::string> data_sources = {
        "document.cookie", "localStorage", "sessionStorage",
        "navigator.userAgent", "screen.width", "screen.height",
        "navigator.language", "navigator.plugins"
    };
    for (const auto& src : data_sources) {
        if (script.find(src) != std::string::npos) {
            data_access_count++;
        }
    }
    if (data_access_count >= 4) score += 0.3;

    return std::min(score, 1.0);
}

// ============================================================
// 스키머 탐지
// ============================================================

double ScriptAnalyzer::detectSkimmer(const std::string& script) {
    double score = 0.0;
    std::string lower = script;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (const auto& [pattern, desc] : skimmer_patterns_) {
        if (std::regex_search(lower, pattern)) {
            score += 0.2;
        }
    }

    // 결제 필드 이름 패턴 + 외부 전송
    std::vector<std::string> payment_fields = {
        "cardnumber", "card-number", "cc-number", "ccnum",
        "cvv", "cvc", "card_number", "expiry", "exp-date",
        "billing-address", "cardholder"
    };
    int field_matches = 0;
    for (const auto& field : payment_fields) {
        if (lower.find(field) != std::string::npos) {
            field_matches++;
        }
    }

    if (field_matches >= 2) score += 0.2;
    if (field_matches >= 4) score += 0.2;

    // 결제 필드 + 인코딩 + 전송
    bool has_encode = (lower.find("btoa(") != std::string::npos ||
                       lower.find("json.stringify") != std::string::npos ||
                       lower.find("encodeuricomponent") != std::string::npos);
    bool has_send = (lower.find("fetch(") != std::string::npos ||
                     lower.find("xmlhttprequest") != std::string::npos);

    if (field_matches >= 2 && has_encode && has_send) {
        score += 0.3;
    }

    return std::min(score, 1.0);
}

// ============================================================
// 난독화 탐지
// ============================================================

double ScriptAnalyzer::detectObfuscation(const std::string& script) {
    double score = 0.0;

    // 패턴 매칭
    for (const auto& [pattern, desc] : obfuscation_patterns_) {
        if (std::regex_search(script, pattern)) {
            score += 0.15;
        }
    }

    // 엔트로피 기반 분석 (높은 엔트로피 = 난독화 가능성)
    double entropy = calculateEntropy(script);
    if (entropy > 5.5) score += 0.1;
    if (entropy > 6.0) score += 0.15;
    if (entropy > 6.5) score += 0.2;

    // 평균 식별자 길이 분석 (난독화 코드는 짧은 변수명 사용)
    std::regex identifier_re(R"(\b[a-zA-Z_$][a-zA-Z0-9_$]*\b)");
    auto ids_begin = std::sregex_iterator(script.begin(), script.end(), identifier_re);
    auto ids_end = std::sregex_iterator();

    int total_len = 0;
    int id_count = 0;
    for (auto it = ids_begin; it != ids_end; ++it) {
        total_len += static_cast<int>(it->str().length());
        id_count++;
        if (id_count > 500) break;  // 샘플링 제한
    }

    if (id_count > 0) {
        double avg_len = static_cast<double>(total_len) / id_count;
        if (avg_len < 3.0) score += 0.15;  // 매우 짧은 변수명
    }

    // 한 줄의 평균 길이 (난독화 코드는 매우 긴 한 줄)
    int line_count = static_cast<int>(std::count(script.begin(), script.end(), '\n')) + 1;
    double avg_line_len = static_cast<double>(script.size()) / line_count;
    if (avg_line_len > 1000) score += 0.15;
    if (avg_line_len > 5000) score += 0.15;

    // 문자열 리터럴 비율 (난독화 코드는 인코딩된 문자열이 많음)
    int string_chars = 0;
    bool in_string = false;
    char string_delim = '\0';
    for (size_t i = 0; i < script.size(); ++i) {
        if (!in_string && (script[i] == '"' || script[i] == '\'')) {
            in_string = true;
            string_delim = script[i];
        } else if (in_string && script[i] == string_delim && (i == 0 || script[i-1] != '\\')) {
            in_string = false;
        }
        if (in_string) string_chars++;
    }
    double string_ratio = static_cast<double>(string_chars) / script.size();
    if (string_ratio > 0.6) score += 0.1;

    return std::min(score, 1.0);
}

// ============================================================
// 엔트로피 계산 (Shannon Entropy)
// ============================================================

double ScriptAnalyzer::calculateEntropy(const std::string& data) {
    if (data.empty()) return 0.0;

    // 바이트별 빈도 계산
    std::array<int, 256> freq{};
    for (unsigned char c : data) {
        freq[c]++;
    }

    // 섀넌 엔트로피 계산
    double entropy = 0.0;
    double size = static_cast<double>(data.size());

    for (int f : freq) {
        if (f > 0) {
            double p = static_cast<double>(f) / size;
            entropy -= p * std::log2(p);
        }
    }

    return entropy;  // 0 ~ 8 범위
}

// ============================================================
// 악성 해시 확인
// ============================================================

bool ScriptAnalyzer::isKnownMalware(const std::string& script) const {
    std::string hash = computeHash(script);
    return malware_hashes_.contains(hash);
}

void ScriptAnalyzer::addMalwareHash(const std::string& hash) {
    malware_hashes_.insert(hash);
}

// ============================================================
// 의심 API 탐색
// ============================================================

std::vector<std::string> ScriptAnalyzer::findSuspiciousApis(const std::string& script) {
    std::vector<std::string> found;

    for (const auto& api : suspicious_apis_) {
        if (script.find(api) != std::string::npos) {
            found.push_back(api);
        }
    }

    return found;
}

// ============================================================
// 해시 계산 (간이 구현 - FNV-1a 기반)
// ============================================================

std::string ScriptAnalyzer::computeHash(const std::string& data) const {
    // FNV-1a 64비트 해시 (간이 - 실제로는 SHA-256 사용 권장)
    uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ULL;  // FNV prime
    }

    // 16진수 문자열로 변환
    static const char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(16);
    for (int i = 60; i >= 0; i -= 4) {
        result += hex_chars[(hash >> i) & 0xF];
    }

    return result;
}

} // namespace ordinal::security
