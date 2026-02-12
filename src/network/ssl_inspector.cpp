/**
 * @file ssl_inspector.cpp
 * @brief SSL/TLS 연결 검사기 구현
 */

#include "ssl_inspector.h"

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include <iostream>
#include <cstring>
#include <sstream>

namespace ordinal::network {

struct SslInspector::Impl {
    std::string ca_bundle_path;
};

SslInspector::SslInspector() : impl_(std::make_unique<Impl>()) {
    // OpenSSL 초기화
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}

SslInspector::~SslInspector() = default;

SslInspectionResult SslInspector::inspect(
    const std::string& hostname,
    int port
) {
    SslInspectionResult result;

    // SSL 컨텍스트 생성
    const SSL_METHOD* method = TLS_client_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) {
        result.errors.push_back("SSL 컨텍스트 생성 실패");
        return result;
    }

    // 최소 TLS 1.2 요구
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    // CA 인증서 로드
    if (!impl_->ca_bundle_path.empty()) {
        SSL_CTX_load_verify_locations(ctx, impl_->ca_bundle_path.c_str(), nullptr);
    } else {
        SSL_CTX_set_default_verify_paths(ctx);
    }

    // TCP 연결 생성
    BIO* bio = BIO_new_ssl_connect(ctx);
    if (!bio) {
        result.errors.push_back("BIO 생성 실패");
        SSL_CTX_free(ctx);
        return result;
    }

    // 호스트:포트 설정
    std::string host_port = hostname + ":" + std::to_string(port);
    BIO_set_conn_hostname(bio, host_port.c_str());

    // SSL 객체 추출 및 SNI 설정
    SSL* ssl = nullptr;
    BIO_get_ssl(bio, &ssl);
    if (ssl) {
        SSL_set_tlsext_host_name(ssl, hostname.c_str());
        // 인증서 검증 모드 설정
        SSL_set_verify(ssl, SSL_VERIFY_PEER, nullptr);
    }

    // 연결 수행
    if (BIO_do_connect(bio) <= 0) {
        result.errors.push_back("SSL 연결 실패: " + hostname);
        unsigned long err = ERR_get_error();
        if (err) {
            char err_buf[256];
            ERR_error_string_n(err, err_buf, sizeof(err_buf));
            result.errors.push_back(std::string("OpenSSL 오류: ") + err_buf);
        }
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return result;
    }

    // SSL 핸드셰이크 수행
    if (BIO_do_handshake(bio) <= 0) {
        result.errors.push_back("SSL 핸드셰이크 실패");
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return result;
    }

    // === 연결 성공! 정보 수집 ===
    result.is_secure = true;

    // TLS 버전
    if (ssl) {
        const char* version = SSL_get_version(ssl);
        if (version) {
            result.tls_version = parseTlsVersion(version);
        }

        // 암호 스위트
        const SSL_CIPHER* cipher = SSL_get_current_cipher(ssl);
        if (cipher) {
            result.cipher_suite = SSL_CIPHER_get_name(cipher);
            result.key_exchange_bits = SSL_CIPHER_get_bits(cipher, nullptr);
        }
    }

    // 서버 인증서 정보 수집
    X509* cert = SSL_get_peer_certificate(ssl);
    if (cert) {
        // Subject
        char subject_buf[256];
        X509_NAME_oneline(X509_get_subject_name(cert), subject_buf, sizeof(subject_buf));
        result.cert_subject = subject_buf;

        // Issuer
        char issuer_buf[256];
        X509_NAME_oneline(X509_get_issuer_name(cert), issuer_buf, sizeof(issuer_buf));
        result.cert_issuer = issuer_buf;

        // Serial Number
        ASN1_INTEGER* serial = X509_get_serialNumber(cert);
        if (serial) {
            BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
            if (bn) {
                char* hex = BN_bn2hex(bn);
                if (hex) {
                    result.cert_serial = hex;
                    OPENSSL_free(hex);
                }
                BN_free(bn);
            }
        }

        // SHA-256 지문
        unsigned char fingerprint[EVP_MAX_MD_SIZE];
        unsigned int fingerprint_len = 0;
        if (X509_digest(cert, EVP_sha256(), fingerprint, &fingerprint_len)) {
            std::ostringstream oss;
            for (unsigned int i = 0; i < fingerprint_len; ++i) {
                if (i > 0) oss << ":";
                oss << std::hex << std::uppercase 
                    << static_cast<int>(fingerprint[i]);
            }
            result.cert_fingerprint_sha256 = oss.str();
        }

        // Subject Alternative Names
        GENERAL_NAMES* san_names = static_cast<GENERAL_NAMES*>(
            X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr)
        );
        if (san_names) {
            int san_count = sk_GENERAL_NAME_num(san_names);
            for (int i = 0; i < san_count; ++i) {
                GENERAL_NAME* entry = sk_GENERAL_NAME_value(san_names, i);
                if (entry->type == GEN_DNS) {
                    const char* dns_name = reinterpret_cast<const char*>(
                        ASN1_STRING_get0_data(entry->d.dNSName)
                    );
                    if (dns_name) {
                        result.cert_san.emplace_back(dns_name);
                    }
                }
            }
            GENERAL_NAMES_free(san_names);
        }

        // 인증서 체인 유효성 검증
        long verify_result = SSL_get_verify_result(ssl);
        result.cert_chain_valid = (verify_result == X509_V_OK);
        if (verify_result != X509_V_OK) {
            result.errors.push_back(
                std::string("인증서 검증 실패: ") + 
                X509_verify_cert_error_string(verify_result)
            );
            result.is_secure = false;
        }

        X509_free(cert);
    } else {
        result.errors.push_back("서버 인증서를 받지 못했습니다.");
        result.is_secure = false;
    }

    // 보안 이슈 확인
    checkSecurityIssues(result);

    // 정리
    BIO_free_all(bio);
    SSL_CTX_free(ctx);

    std::cout << "[SslInspector] " << hostname << " 검사 완료 - 등급: " 
              << result.securityGrade() << std::endl;

    return result;
}

std::optional<int> SslInspector::daysUntilExpiry(const std::string& hostname) {
    auto result = inspect(hostname);
    if (result.cert_valid_until == std::chrono::system_clock::time_point{}) {
        return std::nullopt;
    }

    auto now = std::chrono::system_clock::now();
    auto diff = result.cert_valid_until - now;
    return std::chrono::duration_cast<std::chrono::hours>(diff).count() / 24;
}

bool SslInspector::verifyPin(
    const std::string& hostname,
    const std::string& expected_pin
) {
    auto result = inspect(hostname);
    // 인증서 핀과 비교 (SHA-256 지문 기반)
    // TODO: SPKI 핀 해시 비교로 개선
    return result.cert_fingerprint_sha256 == expected_pin;
}

void SslInspector::setCaBundlePath(const std::string& path) {
    impl_->ca_bundle_path = path;
}

TlsVersion SslInspector::parseTlsVersion(const std::string& version_str) {
    if (version_str == "TLSv1.3") return TlsVersion::TLS_1_3;
    if (version_str == "TLSv1.2") return TlsVersion::TLS_1_2;
    if (version_str == "TLSv1.1") return TlsVersion::TLS_1_1;
    if (version_str == "TLSv1")   return TlsVersion::TLS_1_0;
    return TlsVersion::Unknown;
}

void SslInspector::checkSecurityIssues(SslInspectionResult& result) const {
    // TLS 1.0/1.1은 보안 취약
    if (result.tls_version == TlsVersion::TLS_1_0 ||
        result.tls_version == TlsVersion::TLS_1_1) {
        result.warnings.push_back("구버전 TLS 프로토콜 사용 중 (TLS 1.2 이상 권장)");
    }

    // 약한 암호 스위트 체크
    if (result.cipher_suite.find("RC4") != std::string::npos ||
        result.cipher_suite.find("DES") != std::string::npos ||
        result.cipher_suite.find("MD5") != std::string::npos) {
        result.warnings.push_back("약한 암호 스위트 사용: " + result.cipher_suite);
    }

    // 짧은 키 길이
    if (result.key_exchange_bits > 0 && result.key_exchange_bits < 128) {
        result.warnings.push_back("키 교환 비트 수가 너무 짧습니다: " + 
                                   std::to_string(result.key_exchange_bits) + "비트");
    }

    // 자체 서명 인증서 확인
    if (result.cert_subject == result.cert_issuer && !result.cert_subject.empty()) {
        result.warnings.push_back("자체 서명 인증서입니다.");
    }
}

} // namespace ordinal::network
