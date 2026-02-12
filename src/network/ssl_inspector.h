#pragma once

/**
 * @file ssl_inspector.h
 * @brief SSL/TLS 연결 검사기
 * 
 * HTTPS 연결의 SSL/TLS 세부 정보를 검사하고,
 * 인증서 체인 유효성, 프로토콜 버전, 암호 스위트 등을 분석합니다.
 */

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <chrono>

namespace ordinal::network {

/**
 * @brief TLS 프로토콜 버전
 */
enum class TlsVersion {
    Unknown,
    TLS_1_0,    ///< TLS 1.0 (보안 취약)
    TLS_1_1,    ///< TLS 1.1 (보안 취약)
    TLS_1_2,    ///< TLS 1.2
    TLS_1_3     ///< TLS 1.3 (권장)
};

/**
 * @brief SSL 검사 결과
 */
struct SslInspectionResult {
    bool is_secure{false};                  ///< 보안 연결 여부
    TlsVersion tls_version{TlsVersion::Unknown};
    std::string cipher_suite;               ///< 사용된 암호 스위트
    int key_exchange_bits{0};               ///< 키 교환 비트 수

    // 인증서 정보
    std::string cert_subject;
    std::string cert_issuer;
    std::string cert_serial;
    std::string cert_fingerprint_sha256;
    std::chrono::system_clock::time_point cert_valid_from;
    std::chrono::system_clock::time_point cert_valid_until;
    std::vector<std::string> cert_san;      ///< Subject Alternative Names

    // 인증서 체인
    std::vector<std::string> cert_chain;    ///< 인증서 체인 (PEM)
    bool cert_chain_valid{false};           ///< 체인 유효성

    // 보안 경고
    std::vector<std::string> warnings;      ///< 보안 경고 목록
    std::vector<std::string> errors;        ///< 심각한 오류 목록

    // HSTS
    bool hsts_enabled{false};               ///< HSTS 활성화 여부
    int hsts_max_age{0};                    ///< HSTS max-age

    // OCSP
    bool ocsp_stapled{false};               ///< OCSP Stapling 여부
    std::string ocsp_status;                ///< OCSP 상태

    /**
     * @brief 보안 등급 (A~F)
     */
    [[nodiscard]] char securityGrade() const {
        if (!is_secure || !errors.empty()) return 'F';
        if (tls_version < TlsVersion::TLS_1_2) return 'D';
        if (!warnings.empty()) return 'B';
        if (tls_version == TlsVersion::TLS_1_3 && hsts_enabled) return 'A';
        return 'B';
    }
};

/**
 * @brief SSL/TLS 연결 검사기
 */
class SslInspector {
public:
    SslInspector();
    ~SslInspector();

    /**
     * @brief 호스트의 SSL/TLS 연결 검사
     * @param hostname 호스트명
     * @param port 포트 (기본: 443)
     * @return 검사 결과
     */
    [[nodiscard]] SslInspectionResult inspect(
        const std::string& hostname,
        int port = 443
    );

    /**
     * @brief 인증서 만료일 확인
     * @param hostname 호스트명
     * @return 만료까지 남은 일수 (음수면 이미 만료)
     */
    [[nodiscard]] std::optional<int> daysUntilExpiry(const std::string& hostname);

    /**
     * @brief 인증서 핀닝 검증
     * @param hostname 호스트명
     * @param expected_pin Base64 인코딩된 공개키 핀
     * @return 핀 일치 여부
     */
    [[nodiscard]] bool verifyPin(
        const std::string& hostname,
        const std::string& expected_pin
    );

    /**
     * @brief CA 인증서 경로 설정
     */
    void setCaBundlePath(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    /**
     * @brief TLS 버전 문자열 파싱
     */
    static TlsVersion parseTlsVersion(const std::string& version_str);

    /**
     * @brief 보안 경고 생성
     */
    void checkSecurityIssues(SslInspectionResult& result) const;
};

} // namespace ordinal::network
