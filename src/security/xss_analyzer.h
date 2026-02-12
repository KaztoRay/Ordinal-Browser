#pragma once

/**
 * @file xss_analyzer.h
 * @brief XSS (크로스 사이트 스크립팅) 분석기
 * 
 * 반사형 XSS, 저장형 XSS, DOM 기반 XSS 패턴을 감지합니다.
 * HTML/JavaScript 콘텐츠를 분석하여 스크립트 인젝션 시도를 탐지합니다.
 */

#include "security_agent.h"

#include <string>
#include <vector>
#include <regex>
#include <unordered_map>
#include <functional>

namespace ordinal::security {

/**
 * @brief XSS 공격 유형
 */
enum class XssType {
    Reflected,      ///< 반사형 XSS (URL 파라미터 반영)
    Stored,         ///< 저장형 XSS (서버에 저장된 악성 스크립트)
    DomBased,       ///< DOM 기반 XSS (클라이언트 사이드)
    MutationBased,  ///< 변이 기반 XSS (브라우저 파싱 차이 이용)
    Unknown         ///< 분류 불가
};

/**
 * @brief XSS 탐지 결과 상세
 */
struct XssDetection {
    XssType type{XssType::Unknown};         ///< XSS 유형
    std::string payload;                     ///< 감지된 페이로드
    std::string context;                     ///< 발견된 문맥 (HTML 속성, 스크립트 등)
    int line_number{0};                      ///< 발견 위치 (라인)
    int column{0};                           ///< 발견 위치 (컬럼)
    double confidence{0.0};                  ///< 탐지 신뢰도 (0~1)
    std::string sanitization_advice;         ///< 보안 조치 권고
};

/**
 * @brief XSS 분석 설정
 */
struct XssAnalyzerConfig {
    bool detect_reflected{true};        ///< 반사형 XSS 탐지 활성화
    bool detect_stored{true};           ///< 저장형 XSS 탐지 활성화
    bool detect_dom_based{true};        ///< DOM 기반 XSS 탐지 활성화
    bool detect_mutation{true};         ///< 변이 기반 XSS 탐지 활성화
    bool strict_mode{false};            ///< 엄격 모드 (오탐 가능성 증가)
    double confidence_threshold{0.5};   ///< 보고 최소 신뢰도
};

/**
 * @brief XSS 분석기
 * 
 * HTML, JavaScript, URL 파라미터를 분석하여 XSS 공격 패턴을 탐지합니다.
 * 다양한 인코딩/난독화 기법도 감지합니다.
 */
class XssAnalyzer {
public:
    XssAnalyzer();
    ~XssAnalyzer();

    /**
     * @brief 초기화
     */
    bool initialize(const XssAnalyzerConfig& config = {});

    /**
     * @brief HTML 콘텐츠에서 XSS 분석
     * @param url 페이지 URL (반사형 XSS 탐지용)
     * @param html HTML 소스
     * @return 위협 보고서 목록
     */
    [[nodiscard]] std::vector<ThreatReport> analyzeHtml(
        const std::string& url,
        const std::string& html
    );

    /**
     * @brief JavaScript 코드에서 DOM 기반 XSS 분석
     * @param url 페이지 URL
     * @param script JavaScript 소스
     * @return 위협 보고서 목록
     */
    [[nodiscard]] std::vector<ThreatReport> analyzeScript(
        const std::string& url,
        const std::string& script
    );

    /**
     * @brief URL 파라미터 검사 (반사형 XSS)
     * @param url 전체 URL
     * @return 위험한 파라미터 이름/값 쌍
     */
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> checkUrlParams(
        const std::string& url
    );

    /**
     * @brief 입력 값의 XSS 위험도 평가
     * @param input 사용자 입력 값
     * @return 위험도 (0~1)
     */
    [[nodiscard]] double evaluateInputRisk(const std::string& input);

    /**
     * @brief 상세 XSS 탐지 결과
     */
    [[nodiscard]] std::vector<XssDetection> detailedScan(
        const std::string& url,
        const std::string& html,
        const std::string& script = ""
    );

    /**
     * @brief XSS 페이로드 무력화 (이스케이프)
     * @param input 원본 문자열
     * @return 안전한 문자열
     */
    [[nodiscard]] static std::string sanitize(const std::string& input);

private:
    /**
     * @brief 인라인 이벤트 핸들러 검사 (onclick, onerror 등)
     */
    [[nodiscard]] std::vector<XssDetection> checkInlineHandlers(const std::string& html);

    /**
     * @brief 스크립트 태그 인젝션 검사
     */
    [[nodiscard]] std::vector<XssDetection> checkScriptInjection(const std::string& html);

    /**
     * @brief 위험한 HTML 속성 검사 (href="javascript:", src 등)
     */
    [[nodiscard]] std::vector<XssDetection> checkDangerousAttributes(const std::string& html);

    /**
     * @brief DOM 조작 패턴 검사 (innerHTML, document.write 등)
     */
    [[nodiscard]] std::vector<XssDetection> checkDomManipulation(const std::string& script);

    /**
     * @brief 위험한 소스/싱크 패턴 검사
     */
    [[nodiscard]] std::vector<XssDetection> checkSourceSinkPatterns(const std::string& script);

    /**
     * @brief 인코딩된 XSS 페이로드 디코딩
     */
    [[nodiscard]] std::string decodePayload(const std::string& encoded) const;

    /**
     * @brief URL 쿼리 파라미터 파싱
     */
    [[nodiscard]] std::unordered_map<std::string, std::string> parseQueryParams(
        const std::string& url
    ) const;

    XssAnalyzerConfig config_;

    // XSS 패턴 (정규 표현식)
    std::vector<std::pair<std::regex, std::string>> script_patterns_;     ///< 스크립트 인젝션 패턴
    std::vector<std::pair<std::regex, std::string>> handler_patterns_;    ///< 이벤트 핸들러 패턴
    std::vector<std::pair<std::regex, std::string>> attribute_patterns_;  ///< 위험 속성 패턴
    std::vector<std::pair<std::regex, std::string>> dom_sink_patterns_;   ///< DOM 싱크 패턴
    std::vector<std::pair<std::regex, std::string>> dom_source_patterns_; ///< DOM 소스 패턴
};

} // namespace ordinal::security
