#pragma once

/**
 * @file script_analyzer.h
 * @brief JavaScript 스크립트 정적 분석기
 * 
 * V8 엔진을 활용하여 JavaScript 코드의 악성 패턴을 분석합니다.
 * 난독화 감지, 악성 코드 패턴 매칭, 암호화폐 채굴 스크립트 탐지 등을 수행합니다.
 */

#include "security_agent.h"

#include <string>
#include <vector>
#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace ordinal::security {

/**
 * @brief 악성 스크립트 카테고리
 */
enum class MaliciousScriptType {
    None,               ///< 정상
    CryptoMiner,        ///< 암호화폐 채굴
    KeyLogger,          ///< 키로거
    DataStealer,        ///< 데이터 탈취
    Obfuscated,         ///< 난독화된 코드
    BotNet,             ///< 봇넷 통신
    DriveByDownload,    ///< 자동 다운로드
    ClickJacking,       ///< 클릭재킹
    FormGrabber,        ///< 폼 데이터 탈취
    Ransomware,         ///< 랜섬웨어 (Web)
    Skimmer             ///< 결제 정보 스키밍
};

/**
 * @brief 스크립트 분석 결과 상세
 */
struct ScriptAnalysisResult {
    MaliciousScriptType type{MaliciousScriptType::None};
    double risk_score{0.0};                     ///< 위험도 (0~1)
    std::string summary;                         ///< 분석 요약
    std::vector<std::string> suspicious_apis;    ///< 사용된 의심 API 목록
    std::vector<std::string> indicators;         ///< 악성 지표 목록
    bool is_obfuscated{false};                   ///< 난독화 여부
    double obfuscation_score{0.0};               ///< 난독화 정도 (0~1)
    size_t code_size{0};                         ///< 코드 크기 (바이트)
    int entropy_score{0};                        ///< 엔트로피 점수 (높으면 난독화)
};

/**
 * @brief 스크립트 분석기 설정
 */
struct ScriptAnalyzerConfig {
    bool detect_crypto_miners{true};        ///< 암호화폐 채굴 탐지
    bool detect_keyloggers{true};           ///< 키로거 탐지
    bool detect_data_stealers{true};        ///< 데이터 탈취 탐지
    bool detect_obfuscation{true};          ///< 난독화 탐지
    bool detect_skimmers{true};             ///< 결제 스키머 탐지
    double risk_threshold{0.5};             ///< 보고 최소 위험도
    size_t max_script_size{5 * 1024 * 1024}; ///< 최대 분석 크기 (5MB)
};

/**
 * @brief JavaScript 스크립트 정적 분석기
 * 
 * 정규 표현식 기반 패턴 매칭과 코드 특성 분석을 통해
 * 악성 JavaScript를 탐지합니다.
 */
class ScriptAnalyzer {
public:
    ScriptAnalyzer();
    ~ScriptAnalyzer();

    /**
     * @brief 초기화
     */
    bool initialize(const ScriptAnalyzerConfig& config = {});

    /**
     * @brief JavaScript 코드 분석 (위협 보고서 반환)
     * @param url 스크립트 출처 URL
     * @param script JavaScript 소스 코드
     * @return 위협 보고서 목록
     */
    [[nodiscard]] std::vector<ThreatReport> analyzeScript(
        const std::string& url,
        const std::string& script
    );

    /**
     * @brief 상세 분석 결과
     */
    [[nodiscard]] ScriptAnalysisResult detailedAnalysis(
        const std::string& script
    );

    /**
     * @brief 난독화 여부 판별
     * @param script JavaScript 소스
     * @return 난독화 점수 (0~1, 높을수록 난독화 가능성 높음)
     */
    [[nodiscard]] double detectObfuscation(const std::string& script);

    /**
     * @brief 암호화폐 채굴 스크립트 탐지
     * @param script JavaScript 소스
     * @return 채굴 스크립트 확률 (0~1)
     */
    [[nodiscard]] double detectCryptoMiner(const std::string& script);

    /**
     * @brief 코드 엔트로피 계산 (정보 이론 기반)
     * @param data 분석 대상 문자열
     * @return 엔트로피 값 (0~8, 높으면 무작위/암호화/난독화)
     */
    [[nodiscard]] static double calculateEntropy(const std::string& data);

    /**
     * @brief 알려진 악성 스크립트 해시 확인
     * @param script JavaScript 소스
     * @return 블랙리스트 해시 매치 여부
     */
    [[nodiscard]] bool isKnownMalware(const std::string& script) const;

    /**
     * @brief 악성 스크립트 해시 추가
     */
    void addMalwareHash(const std::string& hash);

private:
    /**
     * @brief 키로거 패턴 탐지
     */
    [[nodiscard]] double detectKeylogger(const std::string& script);

    /**
     * @brief 데이터 탈취 패턴 탐지
     */
    [[nodiscard]] double detectDataStealer(const std::string& script);

    /**
     * @brief 결제 스키머 탐지
     */
    [[nodiscard]] double detectSkimmer(const std::string& script);

    /**
     * @brief 위험한 API 사용 분석
     */
    [[nodiscard]] std::vector<std::string> findSuspiciousApis(const std::string& script);

    /**
     * @brief SHA-256 해시 계산 (간이 구현)
     */
    [[nodiscard]] std::string computeHash(const std::string& data) const;

    ScriptAnalyzerConfig config_;

    // 패턴 그룹
    std::vector<std::pair<std::regex, std::string>> miner_patterns_;     ///< 채굴 패턴
    std::vector<std::pair<std::regex, std::string>> keylogger_patterns_; ///< 키로거 패턴
    std::vector<std::pair<std::regex, std::string>> stealer_patterns_;   ///< 데이터 탈취 패턴
    std::vector<std::pair<std::regex, std::string>> skimmer_patterns_;   ///< 스키머 패턴
    std::vector<std::pair<std::regex, std::string>> obfuscation_patterns_; ///< 난독화 패턴

    // 의심스러운 API 목록
    std::vector<std::string> suspicious_apis_;

    // 알려진 악성 스크립트 해시 (SHA-256)
    std::unordered_set<std::string> malware_hashes_;
};

} // namespace ordinal::security
