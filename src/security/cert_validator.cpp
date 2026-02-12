/**
 * @file cert_validator.cpp
 * @brief SSL/TLS 인증서 검증기 구현
 * 
 * 인증서 체인 검증, HSTS 정책 관리, 호스트명 매칭,
 * 키 강도 검사, 만료 확인 등을 수행합니다.
 */

#include "cert_validator.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>
#include <regex>

namespace ordinal::security {

// ============================================================
// 생성자 / 소멸자
// ============================================================

CertValidator::CertValidator() = default;
CertValidator::~CertValidator() = default;

// ============================================================
// 초기화
// ============================================================

bool CertValidator::initialize(const CertValidatorConfig& config) {
    config_ = config;
    std::cout << "[CertValidator] 인증서 검증기 초기화 중..." << std::endl;

    // 약한 서명 알고리즘 목록
    weak_algorithms_ = {
        "md2WithRSAEncryption", "md5WithRSAEncryption",
        "sha1WithRSAEncryption", "dsaWithSHA1",
        "ecdsa-with-SHA1", "md2", "md4", "md5", "sha1"
    };

    // HSTS 프리로드 도메인 (주요 사이트)
    hsts_preload_ = {
        "google.com", "facebook.com", "twitter.com", "github.com",
        "paypal.com", "stripe.com", "mozilla.org", "python.org",
        "wikipedia.org", "dropbox.com", "1password.com", "bitwarden.com",
        "lastpass.com", "cloudflare.com", "fastly.com",
        "accounts.google.com", "mail.google.com", "drive.google.com",
        // 한국 사이트
        "naver.com", "daum.net", "kakao.com", "toss.im",
        "kakaopay.com", "samsung.com", "coupang.com",
        // 금융
        "chase.com", "bankofamerica.com", "wellsfargo.com",
        "citibank.com", "americanexpress.com",
    };

    // 프리로드 도메인을 HSTS 정책으로 등록
    for (const auto& domain : hsts_preload_) {
        HstsPolicy policy;
        policy.domain = domain;
        policy.max_age = 31536000;  // 1년
        policy.include_subdomains = true;
        policy.preloaded = true;
        policy.observed_at = std::chrono::system_clock::now();
        hsts_policies_[domain] = policy;
    }

    // 기본 인증서 핀 (주요 CA)
    // Google GIAG2
    CertPin google_pin;
    google_pin.domain = "google.com";
    google_pin.pin_sha256 = {
        "7HIpactkIAq2Y49orFOOQKurWxmmSFZhBCoQYcRhJ3Y=",
        "YZPgTZ+woNCCCIW3LH2CxQeLzB/1m42QcCTBSdgayjs="
    };
    google_pin.include_subdomains = true;
    google_pin.max_age = 2592000;  // 30일
    cert_pins_["google.com"] = google_pin;

    std::cout << "[CertValidator] ✓ 약한 알고리즘 " << weak_algorithms_.size() << "개 등록" << std::endl;
    std::cout << "[CertValidator] ✓ HSTS 프리로드 " << hsts_preload_.size() << "개 도메인" << std::endl;
    std::cout << "[CertValidator] ✓ 인증서 핀 " << cert_pins_.size() << "개 도메인" << std::endl;
    return true;
}

// ============================================================
// 인증서 검증
// ============================================================

CertValidationResult CertValidator::validate(
    const std::string& hostname,
    const std::string& cert_pem
) {
    CertValidationResult result;

    if (cert_pem.empty()) {
        result.is_valid = false;
        result.error_message = "인증서가 제공되지 않았습니다.";
        return result;
    }

    // 인증서 정보 파싱
    result.cert_info = parseCertificate(cert_pem);

    // 1. 호스트명 매칭 검사
    result.hostname_match = matchHostname(hostname, result.cert_info.subject);
    if (!result.hostname_match) {
        // SAN에서도 확인
        for (const auto& san : result.cert_info.san_domains) {
            if (matchHostname(hostname, san)) {
                result.hostname_match = true;
                break;
            }
        }
    }
    if (!result.hostname_match) {
        result.warnings.push_back("호스트명 불일치: '" + hostname + "' ≠ '" + result.cert_info.subject + "'");
    }

    // 2. 만료 검사
    auto now = std::chrono::system_clock::now();
    result.not_expired = (now >= result.cert_info.not_before && now <= result.cert_info.not_after);
    if (!result.not_expired) {
        if (now < result.cert_info.not_before) {
            result.warnings.push_back("인증서가 아직 유효하지 않습니다.");
        } else {
            result.warnings.push_back("인증서가 만료되었습니다.");
        }
    } else if (isExpiryNear(result.cert_info.not_after)) {
        result.warnings.push_back("인증서 만료가 " + 
            std::to_string(config_.cert_expiry_warning_days) + "일 이내입니다.");
    }

    // 3. 서명 알고리즘 강도
    result.strong_signature = isStrongSignature(result.cert_info.signature_algorithm);
    if (!result.strong_signature) {
        result.warnings.push_back("약한 서명 알고리즘: " + result.cert_info.signature_algorithm);
    }

    // 4. 키 크기 검사
    result.adequate_key_size = isAdequateKeySize(
        result.cert_info.key_algorithm, result.cert_info.key_size
    );
    if (!result.adequate_key_size) {
        result.warnings.push_back("키 크기 부족: " + result.cert_info.key_algorithm + 
            " " + std::to_string(result.cert_info.key_size) + " 비트");
    }

    // 5. 자체 서명 인증서 경고
    if (result.cert_info.is_self_signed) {
        result.warnings.push_back("자체 서명 인증서입니다. 신뢰할 수 있는 CA가 발급하지 않았습니다.");
    }

    // 6. HSTS 확인
    result.hsts_enabled = isHstsEnabled(hostname);

    // 7. 체인 유효성 (간이 구현 - 실제로는 OpenSSL 사용)
    result.chain_valid = !result.cert_info.is_self_signed && result.hostname_match;

    // 종합 판정
    result.is_valid = result.hostname_match && result.not_expired && 
                      result.strong_signature && result.adequate_key_size;

    return result;
}

// ============================================================
// 위협 보고서 생성
// ============================================================

std::vector<ThreatReport> CertValidator::generateThreatReports(
    const std::string& hostname,
    const CertValidationResult& result
) {
    std::vector<ThreatReport> threats;

    if (result.is_valid && result.warnings.empty()) return threats;

    // 호스트명 불일치
    if (!result.hostname_match) {
        ThreatReport report;
        report.category = ThreatCategory::InvalidCert;
        report.severity = ThreatSeverity::Critical;
        report.url = "https://" + hostname;
        report.description = "인증서 호스트명 불일치: 이 인증서는 '" + 
            result.cert_info.subject + "'용이지만 '" + hostname + "'에 접속 중입니다.";
        report.recommendation = "이 사이트에 접속하지 마세요. 중간자 공격(MITM)의 징후일 수 있습니다.";
        report.detector_name = "CertValidator/HostnameMismatch";
        report.confidence = 0.95;
        report.detected_at = std::chrono::system_clock::now();
        threats.push_back(report);
    }

    // 인증서 만료
    if (!result.not_expired) {
        ThreatReport report;
        report.category = ThreatCategory::InvalidCert;
        report.severity = ThreatSeverity::High;
        report.url = "https://" + hostname;
        report.description = "인증서가 만료되었습니다.";
        report.recommendation = "사이트 관리자에게 인증서 갱신을 요청하세요.";
        report.detector_name = "CertValidator/Expired";
        report.confidence = 1.0;
        report.detected_at = std::chrono::system_clock::now();
        threats.push_back(report);
    }

    // 약한 서명 알고리즘
    if (!result.strong_signature) {
        ThreatReport report;
        report.category = ThreatCategory::InvalidCert;
        report.severity = ThreatSeverity::Medium;
        report.url = "https://" + hostname;
        report.description = "약한 서명 알고리즘 사용: " + result.cert_info.signature_algorithm;
        report.recommendation = "SHA-256 이상의 강력한 서명 알고리즘을 사용하는 인증서로 교체하세요.";
        report.detector_name = "CertValidator/WeakSignature";
        report.confidence = 0.9;
        report.detected_at = std::chrono::system_clock::now();
        threats.push_back(report);
    }

    // 키 크기 부족
    if (!result.adequate_key_size) {
        ThreatReport report;
        report.category = ThreatCategory::InvalidCert;
        report.severity = ThreatSeverity::Medium;
        report.url = "https://" + hostname;
        report.description = "인증서 키 크기 부족: " + 
            result.cert_info.key_algorithm + " " + 
            std::to_string(result.cert_info.key_size) + " 비트";
        report.recommendation = "RSA 2048비트 또는 ECDSA 256비트 이상의 키를 사용하세요.";
        report.detector_name = "CertValidator/WeakKey";
        report.confidence = 0.9;
        report.detected_at = std::chrono::system_clock::now();
        threats.push_back(report);
    }

    // 자체 서명 인증서
    if (result.cert_info.is_self_signed) {
        ThreatReport report;
        report.category = ThreatCategory::InvalidCert;
        report.severity = ThreatSeverity::High;
        report.url = "https://" + hostname;
        report.description = "자체 서명 인증서가 사용되었습니다.";
        report.recommendation = "신뢰할 수 있는 CA에서 발급한 인증서를 사용하세요.";
        report.detector_name = "CertValidator/SelfSigned";
        report.confidence = 0.85;
        report.detected_at = std::chrono::system_clock::now();
        threats.push_back(report);
    }

    return threats;
}

// ============================================================
// HSTS 관리
// ============================================================

void CertValidator::updateHstsPolicy(
    const std::string& domain,
    const std::string& header_value
) {
    HstsPolicy policy;
    policy.domain = domain;
    policy.observed_at = std::chrono::system_clock::now();

    // 헤더 파싱: "max-age=31536000; includeSubDomains; preload"
    std::string lower = header_value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // max-age 추출
    std::regex max_age_re(R"(max-age\s*=\s*(\d+))");
    std::smatch match;
    if (std::regex_search(lower, match, max_age_re)) {
        try {
            policy.max_age = std::stoi(match[1].str());
        } catch (...) {
            policy.max_age = 0;
        }
    }

    // includeSubDomains 확인
    policy.include_subdomains = (lower.find("includesubdomains") != std::string::npos);

    // preload 확인
    policy.preloaded = (lower.find("preload") != std::string::npos);

    // max-age가 0이면 HSTS 비활성화
    if (policy.max_age == 0) {
        std::lock_guard<std::mutex> lock(hsts_mutex_);
        hsts_policies_.erase(domain);
        std::cout << "[CertValidator] HSTS 비활성화: " << domain << std::endl;
    } else {
        std::lock_guard<std::mutex> lock(hsts_mutex_);
        hsts_policies_[domain] = policy;
    }
}

bool CertValidator::isHstsEnabled(const std::string& domain) const {
    std::lock_guard<std::mutex> lock(hsts_mutex_);

    // 정확한 도메인 매칭
    auto it = hsts_policies_.find(domain);
    if (it != hsts_policies_.end()) {
        // 만료 확인
        auto elapsed = std::chrono::system_clock::now() - it->second.observed_at;
        auto elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        if (elapsed_secs <= it->second.max_age || it->second.preloaded) {
            return true;
        }
    }

    // 상위 도메인의 includeSubDomains 확인
    std::string check = domain;
    auto dot_pos = check.find('.');
    while (dot_pos != std::string::npos) {
        std::string parent = check.substr(dot_pos + 1);
        auto parent_it = hsts_policies_.find(parent);
        if (parent_it != hsts_policies_.end() && parent_it->second.include_subdomains) {
            auto elapsed = std::chrono::system_clock::now() - parent_it->second.observed_at;
            auto elapsed_secs = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            if (elapsed_secs <= parent_it->second.max_age || parent_it->second.preloaded) {
                return true;
            }
        }
        check = parent;
        dot_pos = check.find('.');
    }

    return false;
}

std::optional<HstsPolicy> CertValidator::getHstsPolicy(const std::string& domain) const {
    std::lock_guard<std::mutex> lock(hsts_mutex_);
    auto it = hsts_policies_.find(domain);
    if (it != hsts_policies_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> CertValidator::shouldUpgradeToHttps(const std::string& url) const {
    if (!url.starts_with("http://")) return std::nullopt;

    // URL에서 도메인 추출
    std::string domain = url.substr(7);  // "http://" 제거
    auto slash_pos = domain.find('/');
    auto colon_pos = domain.find(':');
    if (slash_pos != std::string::npos) domain = domain.substr(0, slash_pos);
    if (colon_pos != std::string::npos) domain = domain.substr(0, colon_pos);

    // 소문자 변환
    std::transform(domain.begin(), domain.end(), domain.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (isHstsEnabled(domain)) {
        // HTTP → HTTPS로 업그레이드
        std::string https_url = "https://" + url.substr(7);
        return https_url;
    }

    return std::nullopt;
}

// ============================================================
// 인증서 핀닝
// ============================================================

void CertValidator::addCertPin(const CertPin& pin) {
    std::lock_guard<std::mutex> lock(pins_mutex_);
    cert_pins_[pin.domain] = pin;
}

bool CertValidator::verifyPin(
    const std::string& domain,
    const std::string& cert_fingerprint_sha256
) const {
    std::lock_guard<std::mutex> lock(pins_mutex_);

    // 정확한 도메인 매칭
    auto it = cert_pins_.find(domain);
    if (it == cert_pins_.end()) {
        // 상위 도메인에서 includeSubDomains 핀 확인
        std::string check = domain;
        auto dot_pos = check.find('.');
        while (dot_pos != std::string::npos) {
            std::string parent = check.substr(dot_pos + 1);
            auto parent_it = cert_pins_.find(parent);
            if (parent_it != cert_pins_.end() && parent_it->second.include_subdomains) {
                it = parent_it;
                break;
            }
            check = parent;
            dot_pos = check.find('.');
        }
    }

    if (it == cert_pins_.end()) return true;  // 핀이 없으면 통과

    // 핀 중 하나와 일치하는지 확인
    const auto& pins = it->second.pin_sha256;
    return std::find(pins.begin(), pins.end(), cert_fingerprint_sha256) != pins.end();
}

// ============================================================
// 인증서 파싱 (간이 PEM 파서)
// ============================================================

CertificateInfo CertValidator::parseCertificate(const std::string& pem) const {
    CertificateInfo info;

    // PEM 경계 확인
    if (pem.find("-----BEGIN CERTIFICATE-----") == std::string::npos) {
        return info;
    }

    // 간이 파싱 - 실제로는 OpenSSL의 X509_* API 사용
    // 여기서는 PEM 문자열에서 기본 정보 추출 시뮬레이션

    // Subject/Issuer 추출 시도 (텍스트 형식인 경우)
    auto extract_field = [&pem](const std::string& field) -> std::string {
        auto pos = pem.find(field + "=");
        if (pos != std::string::npos) {
            auto start = pos + field.length() + 1;
            auto end = pem.find_first_of(",\n/", start);
            if (end != std::string::npos) {
                return pem.substr(start, end - start);
            }
        }
        return "";
    };

    info.subject = extract_field("CN");
    info.issuer = extract_field("O");

    // 기본값 설정 (실제 OpenSSL 파싱 시 덮어쓰임)
    if (info.subject.empty()) info.subject = "unknown";
    if (info.issuer.empty()) info.issuer = "unknown";

    info.version = 3;
    info.key_algorithm = "RSA";
    info.key_size = 2048;
    info.signature_algorithm = "sha256WithRSAEncryption";

    // 자체 서명 확인 (Subject == Issuer)
    info.is_self_signed = (info.subject == info.issuer);

    // 와일드카드 확인
    info.is_wildcard = info.subject.starts_with("*.");

    // 유효 기간 기본값 (현재 시점 기준 1년)
    auto now = std::chrono::system_clock::now();
    info.not_before = now - std::chrono::hours(24 * 30);  // 30일 전 시작
    info.not_after = now + std::chrono::hours(24 * 335);   // 335일 후 만료

    return info;
}

// ============================================================
// 호스트명 매칭
// ============================================================

bool CertValidator::matchHostname(
    const std::string& hostname,
    const std::string& cert_name
) {
    if (hostname.empty() || cert_name.empty()) return false;

    // 대소문자 무시 비교
    std::string lower_host = hostname;
    std::string lower_cert = cert_name;
    std::transform(lower_host.begin(), lower_host.end(), lower_host.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::transform(lower_cert.begin(), lower_cert.end(), lower_cert.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // 정확한 매칭
    if (lower_host == lower_cert) return true;

    // 와일드카드 매칭 (*.example.com → sub.example.com)
    if (lower_cert.starts_with("*.")) {
        std::string cert_suffix = lower_cert.substr(1);  // ".example.com"

        // 호스트명에서 첫 번째 라벨 이후 부분
        auto dot_pos = lower_host.find('.');
        if (dot_pos != std::string::npos) {
            std::string host_suffix = lower_host.substr(dot_pos);
            if (host_suffix == cert_suffix) return true;
        }
    }

    return false;
}

// ============================================================
// 내부 유틸리티
// ============================================================

bool CertValidator::isStrongSignature(const std::string& algorithm) const {
    std::string lower = algorithm;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return !weak_algorithms_.contains(lower);
}

bool CertValidator::isAdequateKeySize(
    const std::string& algorithm,
    int key_size
) const {
    std::string lower = algorithm;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower.find("rsa") != std::string::npos) {
        return key_size >= config_.min_rsa_key_size;
    }
    if (lower.find("ecdsa") != std::string::npos || 
        lower.find("ec") != std::string::npos) {
        return key_size >= config_.min_ecdsa_key_size;
    }
    if (lower.find("ed25519") != std::string::npos) {
        return true;  // Ed25519는 항상 충분
    }

    return key_size >= 2048;  // 기본 최소값
}

bool CertValidator::isExpiryNear(
    const std::chrono::system_clock::time_point& not_after
) const {
    auto now = std::chrono::system_clock::now();
    auto remaining = not_after - now;
    auto remaining_days = std::chrono::duration_cast<std::chrono::hours>(remaining).count() / 24;

    return remaining_days <= config_.cert_expiry_warning_days;
}

} // namespace ordinal::security
