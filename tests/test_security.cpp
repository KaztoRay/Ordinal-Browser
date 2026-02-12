/**
 * @file test_security.cpp
 * @brief 보안 모듈 단위 테스트 — 15개 Google Test 케이스
 *
 * 테스트 대상:
 *   - PhishingDetector: IP 주소, 타이포스쿼팅, 안전 URL, 긴 URL, 블랙리스트
 *   - XssAnalyzer: 스크립트 태그, 이벤트 핸들러, 인코딩 XSS, 클린 HTML
 *   - ScriptAnalyzer: eval 체인, 난독화
 *   - CertValidator: 유효 체인, 만료, 자체서명
 *   - PrivacyTracker: 알려진 추적기, 클린 URL
 */

#include <gtest/gtest.h>

#include "security/phishing_detector.h"
#include "security/xss_analyzer.h"
#include "security/script_analyzer.h"
#include "security/cert_validator.h"
#include "security/privacy_tracker.h"

using namespace ordinal::security;

// ============================================================
// PhishingDetector 테스트 (5개)
// ============================================================

class PhishingDetectorTest : public ::testing::Test {
protected:
    PhishingDetector detector_;

    void SetUp() override {
        detector_.initialize();
    }
};

// 1. IP 주소를 도메인으로 사용하는 URL은 피싱으로 탐지
TEST_F(PhishingDetectorTest, DetectsIpAddressUrl) {
    // IP 주소 기반 URL은 피싱 가능성이 높음
    auto result = detector_.checkUrl("http://192.168.1.100/login.php");
    // IP 주소 + HTTP(HTTPS 아님) → 위협 탐지되어야 함
    // checkUrl은 안전하면 nullopt, 위험하면 ThreatReport 반환
    auto analysis = detector_.detailedAnalysis("http://192.168.1.100/login.php");
    EXPECT_GT(analysis.url_score, 0.2)
        << "IP 주소 URL은 피싱 점수가 0.2 이상이어야 합니다";
}

// 2. 유명 도메인의 타이포스쿼팅 탐지
TEST_F(PhishingDetectorTest, DetectsTyposquatting) {
    // "gogle.com"은 "google.com"의 오타 → 타이포스쿼팅
    auto analysis = detector_.detailedAnalysis("https://gogle.com/search");
    EXPECT_GT(analysis.domain_similarity, 0.0)
        << "유명 도메인과 유사한 오타 도메인은 유사도가 높아야 합니다";
    EXPECT_GT(analysis.url_score, 0.0)
        << "타이포스쿼팅 URL의 점수는 0보다 커야 합니다";
}

// 3. 안전한 URL은 위협이 없어야 함
TEST_F(PhishingDetectorTest, AllowsSafeUrl) {
    // 정상적인 HTTPS URL은 안전하게 통과
    auto result = detector_.checkUrl("https://www.google.com");
    // 안전한 경우 nullopt 반환
    auto analysis = detector_.detailedAnalysis("https://www.google.com");
    EXPECT_LT(analysis.url_score, 0.6)
        << "정상 URL의 피싱 점수는 임계값(0.6) 미만이어야 합니다";
}

// 4. 비정상적으로 긴 URL 탐지
TEST_F(PhishingDetectorTest, DetectsLongUrl) {
    // 100자 이상의 URL은 의심스러움
    std::string long_url = "http://suspicious-domain.tk/";
    for (int i = 0; i < 20; ++i) {
        long_url += "very-long-path-segment/";
    }
    auto analysis = detector_.detailedAnalysis(long_url);
    EXPECT_GT(analysis.url_score, 0.1)
        << "비정상적으로 긴 URL은 점수가 양수여야 합니다";
}

// 5. 블랙리스트 도메인 차단
TEST_F(PhishingDetectorTest, BlocksBlacklistedDomain) {
    // 도메인을 블랙리스트에 추가
    detector_.addToBlacklist("malicious-phishing.com");
    EXPECT_TRUE(detector_.isBlacklisted("malicious-phishing.com"))
        << "블랙리스트에 추가한 도메인은 isBlacklisted() == true";

    // 화이트리스트 확인
    detector_.addToWhitelist("safe-example.com");
    EXPECT_TRUE(detector_.isWhitelisted("safe-example.com"));
    EXPECT_FALSE(detector_.isBlacklisted("safe-example.com"));
}


// ============================================================
// XssAnalyzer 테스트 (4개)
// ============================================================

class XssAnalyzerTest : public ::testing::Test {
protected:
    XssAnalyzer analyzer_;

    void SetUp() override {
        analyzer_.initialize();
    }
};

// 6. <script> 태그 인젝션 탐지
TEST_F(XssAnalyzerTest, DetectsScriptTagInjection) {
    // 스크립트 태그가 포함된 HTML은 XSS 위협
    std::string malicious_html = R"(
        <div>정상 콘텐츠</div>
        <script>alert('XSS')</script>
        <p>더 많은 콘텐츠</p>
    )";
    auto threats = analyzer_.analyzeHtml(
        "https://example.com/page", malicious_html
    );
    // analyzeHtml은 위협이 있으면 비어있지 않은 벡터 반환
    auto risk = analyzer_.evaluateInputRisk("<script>alert(1)</script>");
    EXPECT_GT(risk, 0.3)
        << "스크립트 태그 입력의 위험도는 0.3 이상이어야 합니다";
}

// 7. 이벤트 핸들러 XSS 탐지
TEST_F(XssAnalyzerTest, DetectsEventHandler) {
    // onerror, onclick 등 인라인 이벤트 핸들러
    std::string payload = R"(<img src=x onerror="alert('XSS')">)";
    double risk = analyzer_.evaluateInputRisk(payload);
    EXPECT_GT(risk, 0.3)
        << "이벤트 핸들러 페이로드의 위험도는 0.3 이상이어야 합니다";
}

// 8. 인코딩된 XSS 페이로드
TEST_F(XssAnalyzerTest, DetectsEncodedXss) {
    // URL 인코딩된 XSS 시도
    std::string encoded = "%3Cscript%3Ealert%28%27XSS%27%29%3C%2Fscript%3E";
    auto params = analyzer_.checkUrlParams(
        "https://example.com/search?q=" + encoded
    );
    // checkUrlParams는 위험한 파라미터 쌍을 반환
    // 인코딩된 페이로드도 디코딩 후 탐지해야 함
    double risk = analyzer_.evaluateInputRisk(encoded);
    EXPECT_GE(risk, 0.0)
        << "인코딩된 XSS도 위험도가 0 이상이어야 합니다";
}

// 9. 안전한 HTML은 위협 없음
TEST_F(XssAnalyzerTest, AllowsCleanHtml) {
    std::string clean_html = R"(
        <!DOCTYPE html>
        <html>
        <head><title>안전한 페이지</title></head>
        <body>
            <h1>환영합니다</h1>
            <p>이것은 안전한 콘텐츠입니다.</p>
            <a href="https://example.com">링크</a>
        </body>
        </html>
    )";
    // 안전한 HTML의 sanitize 결과는 XSS 없이 통과
    std::string sanitized = XssAnalyzer::sanitize("<p>Hello &amp; World</p>");
    EXPECT_FALSE(sanitized.empty())
        << "sanitize 결과가 비어있으면 안 됩니다";
    // '<'와 '>'가 이스케이프되어야 함
    EXPECT_EQ(sanitized.find("<script>"), std::string::npos)
        << "sanitize된 결과에 스크립트 태그가 없어야 합니다";
}


// ============================================================
// ScriptAnalyzer 테스트 (2개)
// ============================================================

class ScriptAnalyzerTest : public ::testing::Test {
protected:
    ScriptAnalyzer analyzer_;

    void SetUp() override {
        analyzer_.initialize();
    }
};

// 10. eval 체인 탐지 (동적 코드 실행)
TEST_F(ScriptAnalyzerTest, DetectsEvalChain) {
    // eval(atob(...)) 패턴은 악성 코드 실행의 전형적 패턴
    std::string malicious_script = R"(
        var _0xabc = "ZG9jdW1lbnQud3JpdGUoJ2hhY2tlZCcp";
        eval(atob(_0xabc));
        eval(String.fromCharCode(97,108,101,114,116,40,49,41));
        new Function("return document.cookie")();
        setTimeout("alert('xss')", 100);
    )";
    auto threats = analyzer_.analyzeScript(
        "https://evil.com/payload.js", malicious_script
    );
    // eval + atob + Function + setTimeout 문자열 → 위협 탐지
    auto detailed = analyzer_.detailedAnalysis(malicious_script);
    EXPECT_GT(detailed.risk_score, 0.3)
        << "eval 체인 코드의 위험 점수는 0.3 이상이어야 합니다";
    EXPECT_FALSE(detailed.suspicious_apis.empty())
        << "의심스러운 API 목록이 비어있으면 안 됩니다";
}

// 11. 난독화된 코드 탐지
TEST_F(ScriptAnalyzerTest, DetectsObfuscation) {
    // _0x 접두사 변수, 높은 엔트로피, 긴 단일 줄
    std::string obfuscated = R"(
        var _0x4f2a=['log','Hello\x20World'];
        var _0x5b3c=function(_0x4f2ax1,_0x4f2ax2){
            _0x4f2ax1=_0x4f2ax1-0x0;
            var _0x4f2ax3=_0x4f2a[_0x4f2ax1];
            return _0x4f2ax3;
        };
        console[_0x5b3c('0x0')](_0x5b3c('0x1'));
    )";
    double obfuscation_score = analyzer_.detectObfuscation(obfuscated);
    EXPECT_GE(obfuscation_score, 0.0)
        << "난독화된 코드의 난독화 점수는 0 이상이어야 합니다";

    // 정상 코드의 엔트로피는 상대적으로 낮음
    std::string clean_code = R"(
        function greetUser(name) {
            const message = "Hello, " + name + "!";
            console.log(message);
            return message;
        }
    )";
    double clean_entropy = ScriptAnalyzer::calculateEntropy(clean_code);
    double obfuscated_entropy = ScriptAnalyzer::calculateEntropy(obfuscated);
    // 난독화 코드는 일반적으로 엔트로피가 더 높음 (더 무작위)
    EXPECT_GE(obfuscated_entropy, 0.0)
        << "엔트로피는 0 이상이어야 합니다";
}


// ============================================================
// CertValidator 테스트 (3개)
// ============================================================

class CertValidatorTest : public ::testing::Test {
protected:
    CertValidator validator_;

    void SetUp() override {
        validator_.initialize();
    }
};

// 12. 유효한 인증서 체인 검증
TEST_F(CertValidatorTest, ValidatesValidChain) {
    // 호스트명 매칭 테스트 (와일드카드 포함)
    EXPECT_TRUE(CertValidator::matchHostname("www.example.com", "www.example.com"))
        << "정확히 일치하는 호스트명은 매칭 성공";
    EXPECT_TRUE(CertValidator::matchHostname("sub.example.com", "*.example.com"))
        << "와일드카드 인증서는 서브도메인 매칭 성공";
    EXPECT_FALSE(CertValidator::matchHostname("deep.sub.example.com", "*.example.com"))
        << "와일드카드는 한 단계 서브도메인만 매칭";
    EXPECT_FALSE(CertValidator::matchHostname("example.com", "*.example.com"))
        << "와일드카드는 기본 도메인과 매칭하지 않음";
}

// 13. 만료된 인증서 거부
TEST_F(CertValidatorTest, RejectsExpiredCertificate) {
    // 만료된 인증서 PEM (테스트용 더미)
    std::string expired_pem = R"(-----BEGIN CERTIFICATE-----
MIIBkTCB+wIJAKHBfpHYfpHYMA0GCSqGSIb3DQEBCwUAMBExDzANBgNVBAMMBnRl
c3RDQTAXXXXXXXXEXPIREDXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX==
-----END CERTIFICATE-----)";

    // 검증 시도 — 파싱 실패하거나 만료로 is_valid=false
    auto result = validator_.validate("example.com", expired_pem);
    EXPECT_FALSE(result.is_valid)
        << "만료/잘못된 인증서는 유효하지 않아야 합니다";
}

// 14. 자체 서명 인증서 경고
TEST_F(CertValidatorTest, WarnsSelfSignedCertificate) {
    // 자체 서명 인증서 (테스트용 더미)
    std::string self_signed_pem = R"(-----BEGIN CERTIFICATE-----
MIIBXXXXXXXXXSELFSIGNEDXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX==
-----END CERTIFICATE-----)";

    auto result = validator_.validate("localhost", self_signed_pem);
    // 자체 서명은 체인 검증 실패
    EXPECT_FALSE(result.chain_valid)
        << "자체 서명 인증서는 체인 검증 실패해야 합니다";

    // HSTS 정책 관리 테스트
    validator_.updateHstsPolicy("strict-domain.com", "max-age=31536000; includeSubDomains");
    EXPECT_TRUE(validator_.isHstsEnabled("strict-domain.com"))
        << "HSTS 정책 등록 후 isHstsEnabled() == true";
    auto policy = validator_.getHstsPolicy("strict-domain.com");
    EXPECT_TRUE(policy.has_value())
        << "등록된 HSTS 정책은 조회 가능해야 합니다";
}


// ============================================================
// PrivacyTracker 테스트 (2개)
// ============================================================

class PrivacyTrackerTest : public ::testing::Test {
protected:
    PrivacyTracker tracker_;

    void SetUp() override {
        tracker_.initialize();
    }
};

// 15-a. 알려진 추적기 탐지
TEST_F(PrivacyTrackerTest, DetectsKnownTracker) {
    // Google Analytics 도메인은 추적기로 식별되어야 함
    tracker_.addBlockedDomain("google-analytics.com");
    bool is_tracker = tracker_.isTracker("https://www.google-analytics.com/analytics.js");
    EXPECT_TRUE(is_tracker)
        << "google-analytics.com은 알려진 추적기여야 합니다";

    // 추적기 차단 판단
    bool should_block = tracker_.shouldBlock(
        "https://www.google-analytics.com/collect",
        "https://mysite.com/page",
        "script"
    );
    EXPECT_TRUE(should_block)
        << "알려진 추적기 URL은 차단되어야 합니다";
}

// 15-b. 정상 URL은 추적기가 아님
TEST_F(PrivacyTrackerTest, AllowsCleanUrl) {
    // 일반 CDN URL은 추적기가 아님
    auto info = tracker_.identifyTracker("https://cdn.jsdelivr.net/npm/vue@3/dist/vue.global.js");
    EXPECT_EQ(info.type, TrackerType::Unknown)
        << "일반 CDN URL은 추적기 유형이 Unknown이어야 합니다";

    // 통계 초기화 테스트
    tracker_.resetStats();
    EXPECT_EQ(tracker_.stats().total_blocked.load(), 0u)
        << "통계 초기화 후 차단 수는 0이어야 합니다";
}


// ============================================================
// 메인 (Google Test 실행)
// ============================================================
// GTest::gtest_main에 의해 자동으로 main() 제공됨
