#pragma once

/**
 * @file security_agent.h
 * @brief 보안 에이전트 (메인 코디네이터)
 * 
 * 모든 보안 서브시스템을 통합 관리하는 중앙 코디네이터입니다.
 * 피싱 탐지, XSS 분석, 스크립트 분석, 프라이버시 추적기 차단,
 * 인증서 검증 등을 조율하고, Python LLM Agent와 gRPC로 통신합니다.
 */

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <queue>

namespace ordinal::security {

// 전방 선언
class PhishingDetector;
class XssAnalyzer;
class ScriptAnalyzer;
class PrivacyTracker;
class CertValidator;

/**
 * @brief 위협 심각도
 */
enum class ThreatSeverity {
    Safe,           ///< 안전
    Info,           ///< 정보 (참고용)
    Low,            ///< 낮음
    Medium,         ///< 중간
    High,           ///< 높음
    Critical        ///< 치명적 (즉시 차단)
};

/**
 * @brief 위협 카테고리
 */
enum class ThreatCategory {
    None,
    Phishing,           ///< 피싱
    Malware,            ///< 악성코드
    XSS,                ///< 크로스 사이트 스크립팅
    SqlInjection,       ///< SQL 인젝션
    InvalidCert,        ///< 잘못된 인증서
    MixedContent,       ///< 혼합 콘텐츠
    Tracker,            ///< 추적기
    Fingerprinting,     ///< 브라우저 핑거프린팅
    DataExfiltration,   ///< 데이터 유출 시도
    CryptoMining,       ///< 암호화폐 채굴
    SuspiciousScript    ///< 의심스러운 스크립트
};

/**
 * @brief 위협 보고서
 */
struct ThreatReport {
    ThreatCategory category{ThreatCategory::None};
    ThreatSeverity severity{ThreatSeverity::Safe};
    std::string url;                        ///< 관련 URL
    std::string description;                ///< 위협 설명
    std::string recommendation;             ///< 권장 조치
    std::string detector_name;              ///< 탐지한 시스템 이름
    double confidence{0.0};                 ///< 신뢰도 (0.0~1.0)
    std::chrono::system_clock::time_point detected_at;  ///< 탐지 시각
    std::unordered_map<std::string, std::string> metadata;  ///< 추가 메타데이터

    /**
     * @brief 위협 심각도 문자열 변환
     */
    [[nodiscard]] std::string severityString() const {
        switch (severity) {
            case ThreatSeverity::Safe:     return "안전";
            case ThreatSeverity::Info:     return "정보";
            case ThreatSeverity::Low:      return "낮음";
            case ThreatSeverity::Medium:   return "중간";
            case ThreatSeverity::High:     return "높음";
            case ThreatSeverity::Critical: return "치명적";
        }
        return "알 수 없음";
    }

    /**
     * @brief 위협 카테고리 문자열 변환
     */
    [[nodiscard]] std::string categoryString() const {
        switch (category) {
            case ThreatCategory::None:            return "없음";
            case ThreatCategory::Phishing:        return "피싱";
            case ThreatCategory::Malware:         return "악성코드";
            case ThreatCategory::XSS:             return "XSS";
            case ThreatCategory::SqlInjection:    return "SQL 인젝션";
            case ThreatCategory::InvalidCert:     return "인증서 오류";
            case ThreatCategory::MixedContent:    return "혼합 콘텐츠";
            case ThreatCategory::Tracker:         return "추적기";
            case ThreatCategory::Fingerprinting:  return "핑거프린팅";
            case ThreatCategory::DataExfiltration:return "데이터 유출";
            case ThreatCategory::CryptoMining:    return "암호화폐 채굴";
            case ThreatCategory::SuspiciousScript:return "의심 스크립트";
        }
        return "알 수 없음";
    }
};

/**
 * @brief 위협 알림 콜백
 */
using ThreatCallback = std::function<void(const ThreatReport&)>;

/**
 * @brief 보안 에이전트 설정
 */
struct SecurityAgentConfig {
    bool enable_phishing_detection{true};   ///< 피싱 탐지 활성화
    bool enable_xss_detection{true};        ///< XSS 탐지 활성화
    bool enable_script_analysis{true};      ///< 스크립트 분석 활성화
    bool enable_privacy_tracking{true};     ///< 프라이버시 보호 활성화
    bool enable_cert_validation{true};      ///< 인증서 검증 활성화
    bool enable_llm_analysis{false};        ///< LLM 기반 심층 분석 활성화
    std::string grpc_server_address{"localhost:50051"};  ///< Python Agent gRPC 주소
    int max_threat_history{1000};            ///< 최대 위협 기록 수
};

/**
 * @brief 보안 에이전트 (중앙 코디네이터)
 * 
 * 모든 보안 서브시스템을 초기화하고, 위협 탐지 결과를 통합합니다.
 * 싱글톤으로 운영되며, 모든 탭/페이지에서 공유됩니다.
 */
class SecurityAgent {
public:
    /**
     * @brief 싱글톤 인스턴스
     */
    static SecurityAgent& instance();

    SecurityAgent(const SecurityAgent&) = delete;
    SecurityAgent& operator=(const SecurityAgent&) = delete;

    /**
     * @brief 보안 에이전트 초기화
     * @param config 설정
     * @return 성공 여부
     */
    bool initialize(const SecurityAgentConfig& config = {});

    /**
     * @brief 종료
     */
    void shutdown();

    // ============================
    // URL/페이지 분석
    // ============================

    /**
     * @brief URL 안전성 검사 (빠른 체크)
     * @param url 검사할 URL
     * @return 위협 보고서 목록 (빈 목록이면 안전)
     */
    [[nodiscard]] std::vector<ThreatReport> analyzeUrl(const std::string& url);

    /**
     * @brief 페이지 콘텐츠 분석 (심층 검사)
     * @param url 페이지 URL
     * @param html_content HTML 소스
     * @param scripts JavaScript 코드 목록
     * @return 위협 보고서 목록
     */
    [[nodiscard]] std::vector<ThreatReport> analyzePage(
        const std::string& url,
        const std::string& html_content,
        const std::vector<std::string>& scripts = {}
    );

    /**
     * @brief 네트워크 요청 검사
     * @param url 요청 URL
     * @param referer 리퍼러 URL
     * @return 차단 여부
     */
    [[nodiscard]] bool shouldBlockRequest(
        const std::string& url,
        const std::string& referer = ""
    );

    // ============================
    // 위협 알림
    // ============================

    /**
     * @brief 위협 콜백 등록
     */
    void onThreatDetected(ThreatCallback callback);

    /**
     * @brief 위협 기록 조회
     */
    [[nodiscard]] std::vector<ThreatReport> threatHistory() const;

    /**
     * @brief 위협 기록 초기화
     */
    void clearThreatHistory();

    // ============================
    // 서브시스템 접근
    // ============================

    [[nodiscard]] PhishingDetector* phishingDetector() const { return phishing_detector_.get(); }
    [[nodiscard]] XssAnalyzer* xssAnalyzer() const { return xss_analyzer_.get(); }
    [[nodiscard]] ScriptAnalyzer* scriptAnalyzer() const { return script_analyzer_.get(); }
    [[nodiscard]] PrivacyTracker* privacyTracker() const { return privacy_tracker_.get(); }
    [[nodiscard]] CertValidator* certValidator() const { return cert_validator_.get(); }

    // ============================
    // 통계
    // ============================

    [[nodiscard]] uint64_t totalScanned() const { return total_scanned_.load(); }
    [[nodiscard]] uint64_t totalThreatsDetected() const { return total_threats_.load(); }
    [[nodiscard]] uint64_t totalBlocked() const { return total_blocked_.load(); }

private:
    SecurityAgent() = default;
    ~SecurityAgent();

    /**
     * @brief 위협 보고서 발행
     */
    void reportThreat(const ThreatReport& report);

    SecurityAgentConfig config_;
    bool initialized_{false};

    // 서브시스템
    std::unique_ptr<PhishingDetector> phishing_detector_;
    std::unique_ptr<XssAnalyzer> xss_analyzer_;
    std::unique_ptr<ScriptAnalyzer> script_analyzer_;
    std::unique_ptr<PrivacyTracker> privacy_tracker_;
    std::unique_ptr<CertValidator> cert_validator_;

    // 위협 기록
    mutable std::mutex history_mutex_;
    std::vector<ThreatReport> threat_history_;

    // 콜백
    std::vector<ThreatCallback> threat_callbacks_;

    // 통계
    std::atomic<uint64_t> total_scanned_{0};
    std::atomic<uint64_t> total_threats_{0};
    std::atomic<uint64_t> total_blocked_{0};
};

} // namespace ordinal::security
