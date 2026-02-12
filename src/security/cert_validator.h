#pragma once

/**
 * @file cert_validator.h
 * @brief SSL/TLS 인증서 검증기
 * 
 * 인증서 체인 검증, HSTS(HTTP Strict Transport Security) 확인,
 * Certificate Transparency(CT) 로그 검증, 인증서 핀닝을 수행합니다.
 */

#include "security_agent.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <optional>
#include <mutex>

namespace ordinal::security {

/**
 * @brief 인증서 정보 구조체
 */
struct CertificateInfo {
    std::string subject;                    ///< 주체 (CN)
    std::string issuer;                     ///< 발급자
    std::string serial_number;              ///< 일련 번호
    std::string fingerprint_sha256;         ///< SHA-256 지문
    std::vector<std::string> san_domains;   ///< Subject Alternative Names
    std::chrono::system_clock::time_point not_before;   ///< 유효 시작
    std::chrono::system_clock::time_point not_after;    ///< 유효 만료
    int version{0};                         ///< 인증서 버전
    int key_size{0};                        ///< 키 크기 (비트)
    std::string key_algorithm;              ///< 키 알고리즘 (RSA, ECDSA 등)
    std::string signature_algorithm;        ///< 서명 알고리즘
    bool is_self_signed{false};             ///< 자체 서명 여부
    bool is_ev{false};                      ///< EV (Extended Validation) 여부
    bool is_wildcard{false};                ///< 와일드카드 인증서 여부
};

/**
 * @brief 인증서 검증 결과
 */
struct CertValidationResult {
    bool is_valid{false};                   ///< 전체 유효성
    bool chain_valid{false};                ///< 체인 유효
    bool not_expired{false};                ///< 만료되지 않음
    bool hostname_match{false};             ///< 호스트명 일치
    bool strong_signature{false};           ///< 강력한 서명 알고리즘
    bool adequate_key_size{false};          ///< 충분한 키 크기
    bool ct_compliant{false};              ///< CT 로그 검증 통과
    bool hsts_enabled{false};               ///< HSTS 활성화
    std::string error_message;              ///< 오류 메시지
    CertificateInfo cert_info;              ///< 인증서 정보
    std::vector<std::string> warnings;      ///< 경고 목록
};

/**
 * @brief HSTS 정책 정보
 */
struct HstsPolicy {
    std::string domain;                     ///< 도메인
    int max_age{0};                         ///< max-age (초)
    bool include_subdomains{false};         ///< 서브도메인 포함 여부
    bool preloaded{false};                  ///< 브라우저 프리로드 목록 포함 여부
    std::chrono::system_clock::time_point observed_at; ///< 관측 시각
};

/**
 * @brief 인증서 핀 정보
 */
struct CertPin {
    std::string domain;                     ///< 도메인
    std::vector<std::string> pin_sha256;    ///< 핀 해시 (Base64 SHA-256)
    int max_age{0};                         ///< max-age (초)
    bool include_subdomains{false};         ///< 서브도메인 포함
    std::string report_uri;                 ///< 위반 보고 URI
};

/**
 * @brief 인증서 검증기 설정
 */
struct CertValidatorConfig {
    bool enforce_hsts{true};                ///< HSTS 강제
    bool enforce_ct{false};                 ///< CT 강제 (기본 비활성)
    bool enforce_pins{false};               ///< 인증서 핀닝 강제
    int min_rsa_key_size{2048};             ///< 최소 RSA 키 크기
    int min_ecdsa_key_size{256};            ///< 최소 ECDSA 키 크기
    int cert_expiry_warning_days{30};       ///< 만료 경고 (일 전)
    std::vector<std::string> trusted_roots; ///< 신뢰 루트 인증서 경로
};

/**
 * @brief SSL/TLS 인증서 검증기
 * 
 * 인증서 체인 유효성, 호스트명 매칭, 키 강도, HSTS 정책 등을
 * 종합적으로 검증합니다.
 */
class CertValidator {
public:
    CertValidator();
    ~CertValidator();

    /**
     * @brief 초기화 (신뢰 루트 인증서, HSTS 프리로드 목록 로드)
     */
    bool initialize(const CertValidatorConfig& config = {});

    /**
     * @brief 인증서 검증 (종합)
     * @param hostname 접속 호스트명
     * @param cert_pem PEM 형식 인증서 (체인 포함)
     * @return 검증 결과
     */
    [[nodiscard]] CertValidationResult validate(
        const std::string& hostname,
        const std::string& cert_pem
    );

    /**
     * @brief 위협 보고서 생성
     * @param hostname 호스트명
     * @param result 검증 결과
     * @return 위협 보고서 (안전하면 빈 목록)
     */
    [[nodiscard]] std::vector<ThreatReport> generateThreatReports(
        const std::string& hostname,
        const CertValidationResult& result
    );

    // ============================
    // HSTS 관리
    // ============================

    /**
     * @brief HSTS 정책 업데이트 (응답 헤더에서)
     * @param domain 도메인
     * @param header_value Strict-Transport-Security 헤더 값
     */
    void updateHstsPolicy(const std::string& domain, const std::string& header_value);

    /**
     * @brief HSTS 적용 여부 확인
     * @param domain 도메인
     * @return HSTS 활성화 여부
     */
    [[nodiscard]] bool isHstsEnabled(const std::string& domain) const;

    /**
     * @brief HSTS 정책 조회
     */
    [[nodiscard]] std::optional<HstsPolicy> getHstsPolicy(const std::string& domain) const;

    /**
     * @brief HTTPS로 업그레이드해야 하는지 확인
     * @param url HTTP URL
     * @return HTTPS URL (업그레이드 필요 시) 또는 nullopt
     */
    [[nodiscard]] std::optional<std::string> shouldUpgradeToHttps(const std::string& url) const;

    // ============================
    // 인증서 핀닝
    // ============================

    /**
     * @brief 인증서 핀 등록
     */
    void addCertPin(const CertPin& pin);

    /**
     * @brief 인증서 핀 검증
     */
    [[nodiscard]] bool verifyPin(
        const std::string& domain,
        const std::string& cert_fingerprint_sha256
    ) const;

    // ============================
    // 인증서 정보 파싱
    // ============================

    /**
     * @brief PEM 인증서에서 정보 추출
     * @param pem PEM 형식 인증서 문자열
     * @return 인증서 정보
     */
    [[nodiscard]] CertificateInfo parseCertificate(const std::string& pem) const;

    /**
     * @brief 호스트명 매칭 (와일드카드 지원)
     * @param hostname 접속 호스트명
     * @param cert_name 인증서 CN/SAN
     * @return 매칭 여부
     */
    [[nodiscard]] static bool matchHostname(
        const std::string& hostname,
        const std::string& cert_name
    );

private:
    /**
     * @brief 서명 알고리즘 강도 확인
     */
    [[nodiscard]] bool isStrongSignature(const std::string& algorithm) const;

    /**
     * @brief 키 크기 충분성 확인
     */
    [[nodiscard]] bool isAdequateKeySize(
        const std::string& algorithm,
        int key_size
    ) const;

    /**
     * @brief 만료 임박 경고 확인
     */
    [[nodiscard]] bool isExpiryNear(
        const std::chrono::system_clock::time_point& not_after
    ) const;

    CertValidatorConfig config_;

    // HSTS 정책 저장소
    mutable std::mutex hsts_mutex_;
    std::unordered_map<std::string, HstsPolicy> hsts_policies_;

    // HSTS 프리로드 도메인
    std::unordered_set<std::string> hsts_preload_;

    // 인증서 핀
    mutable std::mutex pins_mutex_;
    std::unordered_map<std::string, CertPin> cert_pins_;

    // 약한 서명 알고리즘 목록
    std::unordered_set<std::string> weak_algorithms_;
};

} // namespace ordinal::security
