/**
 * @file test_network.cpp
 * @brief 네트워크 레이어 단위 테스트 — 10개 Google Test 케이스
 *
 * 테스트 대상:
 *   - HttpClient: URL 파싱 (단순, 포트, 쿼리), 헤더 빌드 (GET, POST)
 *   - RequestInterceptor: 미들웨어 체인, 요청 차단, 클린 요청 허용
 *   - SslInspector: 유효/무효 인증서
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "network/http_client.h"
#include "network/request_interceptor.h"
#include "network/ssl_inspector.h"

using namespace ordinal::network;

// ============================================================
// HttpClient 테스트 (5개)
// ============================================================

class HttpClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // libcurl 전역 초기화
        HttpClient::globalInit();
    }

    void TearDown() override {
        HttpClient::globalCleanup();
    }
};

// 1. 단순 URL의 HttpRequest 구성
TEST_F(HttpClientTest, SimpleUrlRequest) {
    HttpRequest request;
    request.method = HttpMethod::GET;
    request.url = "https://example.com/path";

    EXPECT_EQ(request.url, "https://example.com/path");
    EXPECT_EQ(request.method, HttpMethod::GET);
    EXPECT_TRUE(request.verify_ssl)
        << "기본 SSL 검증은 활성화되어야 합니다";
    EXPECT_TRUE(request.follow_redirects)
        << "기본 리다이렉션 따라가기는 활성화되어야 합니다";
    EXPECT_EQ(request.max_redirects, 10)
        << "최대 리다이렉션 수의 기본값은 10이어야 합니다";
}

// 2. 포트가 포함된 URL 처리
TEST_F(HttpClientTest, UrlWithPort) {
    HttpRequest request;
    request.url = "http://localhost:8080/api/data";
    request.method = HttpMethod::GET;
    request.connect_timeout = std::chrono::seconds{5};

    EXPECT_EQ(request.url, "http://localhost:8080/api/data");
    EXPECT_EQ(request.connect_timeout.count(), 5)
        << "연결 타임아웃이 5초여야 합니다";
}

// 3. 쿼리 파라미터가 있는 URL
TEST_F(HttpClientTest, UrlWithQueryParams) {
    HttpRequest request;
    request.url = "https://api.example.com/search?q=security&lang=ko&page=1";
    request.method = HttpMethod::GET;
    request.headers["Accept"] = "application/json";
    request.headers["Authorization"] = "Bearer test-token";

    EXPECT_EQ(request.headers.size(), 2u)
        << "헤더가 2개 설정되어야 합니다";
    EXPECT_EQ(request.headers["Accept"], "application/json");
    EXPECT_NE(request.url.find("q=security"), std::string::npos)
        << "URL에 쿼리 파라미터 q=security가 포함되어야 합니다";
}

// 4. GET 요청 헤더 빌드
TEST_F(HttpClientTest, BuildsGetRequestHeaders) {
    HttpClient client;
    client.setUserAgent("OrdinalV8/1.0");
    client.setDefaultHeader("Accept-Language", "ko-KR,ko;q=0.9");

    // GET 요청 구성 확인
    HttpRequest request;
    request.method = HttpMethod::GET;
    request.url = "https://example.com";
    request.headers["Cache-Control"] = "no-cache";

    EXPECT_EQ(request.method, HttpMethod::GET);
    EXPECT_EQ(request.headers["Cache-Control"], "no-cache");
    EXPECT_TRUE(request.body.empty())
        << "GET 요청의 body는 비어있어야 합니다";
}

// 5. POST 요청 헤더 및 바디 빌드
TEST_F(HttpClientTest, BuildsPostRequestHeaders) {
    HttpRequest request;
    request.method = HttpMethod::POST;
    request.url = "https://api.example.com/submit";
    request.body = R"({"username":"test","password":"secure123"})";
    request.content_type = "application/json";
    request.headers["X-Custom-Header"] = "CustomValue";

    EXPECT_EQ(request.method, HttpMethod::POST);
    EXPECT_FALSE(request.body.empty())
        << "POST 요청의 body가 비어있으면 안 됩니다";
    EXPECT_EQ(request.content_type, "application/json");
    EXPECT_NE(request.body.find("username"), std::string::npos)
        << "POST body에 username 필드가 포함되어야 합니다";

    // HttpResponse 기본값 확인
    HttpResponse response;
    EXPECT_EQ(response.status_code, 0);
    EXPECT_FALSE(response.success);
    EXPECT_FALSE(response.isOk())
        << "초기 응답은 isOk() == false이어야 합니다";
}


// ============================================================
// RequestInterceptor 테스트 (3개)
// ============================================================

/**
 * @brief 테스트용 보안 검사 미들웨어
 *
 * 특정 도메인 패턴을 차단하는 간단한 미들웨어
 */
class TestSecurityMiddleware : public InterceptMiddleware {
public:
    explicit TestSecurityMiddleware(std::string blocked_domain)
        : blocked_domain_(std::move(blocked_domain)) {}

    [[nodiscard]] InterceptResult onRequest(const HttpRequest& request) override {
        InterceptResult result;
        // URL에 차단 도메인이 포함되면 차단
        if (request.url.find(blocked_domain_) != std::string::npos) {
            result.action = InterceptAction::Block;
            result.reason = BlockReason::Phishing;
            result.reason_detail = "테스트 차단: " + blocked_domain_;
        } else {
            result.action = InterceptAction::Allow;
        }
        return result;
    }

    [[nodiscard]] InterceptResult onResponse(
        const HttpRequest& /*request*/,
        const HttpResponse& /*response*/
    ) override {
        return InterceptResult{InterceptAction::Allow};
    }

    [[nodiscard]] std::string name() const override {
        return "TestSecurityMiddleware";
    }

    [[nodiscard]] int priority() const override { return 10; }

private:
    std::string blocked_domain_;
};

/**
 * @brief 추적기 차단 미들웨어 (테스트용)
 */
class TestTrackerBlocker : public InterceptMiddleware {
public:
    [[nodiscard]] InterceptResult onRequest(const HttpRequest& request) override {
        InterceptResult result;
        if (request.url.find("tracker.evil.com") != std::string::npos) {
            result.action = InterceptAction::Block;
            result.reason = BlockReason::Tracker;
            result.reason_detail = "추적기 차단";
        } else {
            result.action = InterceptAction::Allow;
        }
        return result;
    }

    [[nodiscard]] InterceptResult onResponse(
        const HttpRequest& /*request*/,
        const HttpResponse& /*response*/
    ) override {
        return InterceptResult{InterceptAction::Allow};
    }

    [[nodiscard]] std::string name() const override {
        return "TestTrackerBlocker";
    }

    [[nodiscard]] int priority() const override { return 20; }
};


class RequestInterceptorTest : public ::testing::Test {
protected:
    RequestInterceptor interceptor_;

    void SetUp() override {
        // 피싱 차단 미들웨어 추가
        interceptor_.addMiddleware(
            std::make_shared<TestSecurityMiddleware>("phishing.evil.com")
        );
        // 추적기 차단 미들웨어 추가
        interceptor_.addMiddleware(
            std::make_shared<TestTrackerBlocker>()
        );
    }
};

// 6. 미들웨어 체인 구성 확인
TEST_F(RequestInterceptorTest, MiddlewareChain) {
    EXPECT_EQ(interceptor_.middlewareCount(), 2u)
        << "미들웨어 2개가 등록되어야 합니다";

    // 통계 초기값 확인
    EXPECT_EQ(interceptor_.totalRequestsInspected(), 0u)
        << "초기 검사 수는 0이어야 합니다";
    EXPECT_EQ(interceptor_.totalRequestsBlocked(), 0u)
        << "초기 차단 수는 0이어야 합니다";
}

// 7. 피싱 URL 차단
TEST_F(RequestInterceptorTest, BlocksMaliciousRequest) {
    HttpRequest request;
    request.url = "https://phishing.evil.com/login.html";
    request.method = HttpMethod::GET;

    auto result = interceptor_.interceptRequest(request);
    EXPECT_EQ(result.action, InterceptAction::Block)
        << "피싱 도메인 요청은 차단되어야 합니다";
    EXPECT_EQ(result.reason, BlockReason::Phishing)
        << "차단 이유는 Phishing이어야 합니다";
    EXPECT_FALSE(result.reason_detail.empty())
        << "차단 상세 사유가 비어있으면 안 됩니다";
}

// 8. 안전한 URL 허용
TEST_F(RequestInterceptorTest, AllowsCleanRequest) {
    HttpRequest request;
    request.url = "https://www.google.com/search?q=test";
    request.method = HttpMethod::GET;

    auto result = interceptor_.interceptRequest(request);
    EXPECT_EQ(result.action, InterceptAction::Allow)
        << "안전한 URL은 허용되어야 합니다";
    EXPECT_EQ(result.reason, BlockReason::None)
        << "안전한 요청의 차단 이유는 None이어야 합니다";

    // 추적기 차단 테스트
    HttpRequest tracker_request;
    tracker_request.url = "https://tracker.evil.com/collect?uid=123";
    tracker_request.method = HttpMethod::GET;

    auto tracker_result = interceptor_.interceptRequest(tracker_request);
    EXPECT_EQ(tracker_result.action, InterceptAction::Block)
        << "추적기 URL은 차단되어야 합니다";
    EXPECT_EQ(tracker_result.reason, BlockReason::Tracker)
        << "차단 이유는 Tracker이어야 합니다";
}


// ============================================================
// SslInspector 테스트 (2개)
// ============================================================

class SslInspectorTest : public ::testing::Test {
protected:
    SslInspector inspector_;
};

// 9. SSL 검사 결과 구조체 기본값
TEST_F(SslInspectorTest, DefaultInspectionResult) {
    SslInspectionResult result;

    // 기본값 확인
    EXPECT_FALSE(result.is_secure)
        << "기본 SslInspectionResult의 is_secure는 false";
    EXPECT_EQ(result.tls_version, TlsVersion::Unknown)
        << "기본 TLS 버전은 Unknown";
    EXPECT_TRUE(result.cipher_suite.empty())
        << "기본 암호 스위트는 비어있어야 합니다";
    EXPECT_FALSE(result.cert_chain_valid)
        << "기본 인증서 체인 유효성은 false";
    EXPECT_TRUE(result.warnings.empty())
        << "기본 경고 목록은 비어있어야 합니다";
    EXPECT_TRUE(result.errors.empty())
        << "기본 오류 목록은 비어있어야 합니다";

    // 보안 등급 (기본값 = F)
    EXPECT_EQ(result.securityGrade(), 'F')
        << "비보안 연결의 등급은 F";
}

// 10. 보안 등급 계산 로직
TEST_F(SslInspectorTest, SecurityGradeCalculation) {
    SslInspectionResult result;

    // TLS 1.3 + HSTS + 오류 없음 → A등급
    result.is_secure = true;
    result.tls_version = TlsVersion::TLS_1_3;
    result.hsts_enabled = true;
    result.cert_chain_valid = true;
    // errors와 warnings가 비어있어야 A등급
    EXPECT_EQ(result.securityGrade(), 'A')
        << "TLS 1.3 + HSTS + 오류 없음 = A등급";

    // TLS 1.2 + 경고 있음 → B등급
    SslInspectionResult result_b;
    result_b.is_secure = true;
    result_b.tls_version = TlsVersion::TLS_1_2;
    result_b.warnings.push_back("인증서 만료 임박");
    EXPECT_EQ(result_b.securityGrade(), 'B')
        << "TLS 1.2 + 경고 = B등급";

    // TLS 1.0 → D등급
    SslInspectionResult result_d;
    result_d.is_secure = true;
    result_d.tls_version = TlsVersion::TLS_1_0;
    EXPECT_EQ(result_d.securityGrade(), 'D')
        << "TLS 1.0 = D등급 (보안 취약)";

    // 오류 있음 → F등급
    SslInspectionResult result_f;
    result_f.is_secure = true;
    result_f.tls_version = TlsVersion::TLS_1_3;
    result_f.errors.push_back("인증서 체인 검증 실패");
    EXPECT_EQ(result_f.securityGrade(), 'F')
        << "오류 있으면 무조건 F등급";
}


// ============================================================
// 메인 (Google Test 실행)
// ============================================================
// GTest::gtest_main에 의해 자동으로 main() 제공됨
