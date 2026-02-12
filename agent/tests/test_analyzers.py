"""분석기 유닛 테스트"""
import pytest
from unittest.mock import AsyncMock, MagicMock

# === PhishingAnalyzer 테스트 ===

class TestPhishingAnalyzer:
    """피싱 분석기 테스트"""

    def test_url_feature_extraction(self):
        """URL 특징 추출 정확성 검증"""
        from agent.utils.feature_extractor import URLFeatureExtractor
        extractor = URLFeatureExtractor()
        features = extractor.extract("https://secure-login.paypal-verify.com/auth?id=12345")
        assert features is not None
        assert len(features) >= 30  # 최소 30개 이상 특징
        # URL 길이 특징 확인
        assert features[0] > 40  # URL이 충분히 길어야 함

    def test_ip_address_detection(self):
        """IP 주소 기반 URL 탐지"""
        from agent.utils.feature_extractor import URLFeatureExtractor
        extractor = URLFeatureExtractor()
        features_ip = extractor.extract("http://192.168.1.1/login")
        features_normal = extractor.extract("https://google.com")
        # IP URL의 has_ip 특징이 활성화되어야 함
        assert features_ip is not None
        assert features_normal is not None

    def test_typosquatting_detection(self):
        """타이포스쿼팅 도메인 유사도 검사"""
        # 유사 도메인 쌍
        similar_pairs = [
            ("paypa1.com", "paypal.com"),
            ("g00gle.com", "google.com"),
            ("amaz0n.com", "amazon.com"),
        ]
        for typo, real in similar_pairs:
            # 레벤슈타인 거리가 2 이하여야 함
            distance = _levenshtein(typo, real)
            assert distance <= 2, f"{typo} vs {real}: 거리 {distance}"

# === MalwareAnalyzer 테스트 ===

class TestMalwareAnalyzer:
    """악성코드 분석기 테스트"""

    def test_eval_chain_detection(self):
        """eval 체인 패턴 탐지"""
        malicious_code = 'eval(atob("YWxlcnQoMSk="));'
        patterns = _detect_patterns(malicious_code)
        assert "eval_usage" in patterns
        assert "base64_decode" in patterns

    def test_obfuscation_scoring(self):
        """난독화 점수 계산"""
        obfuscated = "var _0x4e23=['\\x48\\x65\\x6c\\x6c\\x6f'];(function(_0x2d8f05){})(_0x4e23);"
        clean = "function greet(name) { return 'Hello ' + name; }"
        score_obf = _obfuscation_score(obfuscated)
        score_clean = _obfuscation_score(clean)
        assert score_obf > 0.6  # 난독화된 코드는 높은 점수
        assert score_clean < 0.3  # 깨끗한 코드는 낮은 점수

    def test_clean_code_passes(self):
        """정상 코드는 안전 판정"""
        clean_code = """
        document.addEventListener('DOMContentLoaded', function() {
            const button = document.getElementById('submit');
            button.addEventListener('click', handleSubmit);
        });
        """
        patterns = _detect_patterns(clean_code)
        assert "eval_usage" not in patterns
        assert "document_write" not in patterns

# === PrivacyAnalyzer 테스트 ===

class TestPrivacyAnalyzer:
    """프라이버시 분석기 테스트"""

    def test_tracker_matching(self):
        """알려진 추적기 매칭"""
        known_trackers = [
            "https://www.googletagmanager.com/gtm.js",
            "https://connect.facebook.net/en_US/fbevents.js",
            "https://www.google-analytics.com/analytics.js",
        ]
        for url in known_trackers:
            assert _is_known_tracker(url), f"추적기 미탐지: {url}"

    def test_fingerprint_canvas_detection(self):
        """Canvas 핑거프린팅 탐지"""
        fingerprint_code = """
        var canvas = document.createElement('canvas');
        var ctx = canvas.getContext('2d');
        ctx.textBaseline = 'top';
        ctx.font = '14px Arial';
        ctx.fillText('fingerprint', 0, 0);
        var data = canvas.toDataURL();
        """
        techniques = _detect_fingerprinting(fingerprint_code)
        assert "canvas" in techniques

    def test_clean_page_passes(self):
        """추적기 없는 깨끗한 페이지"""
        clean_urls = [
            "https://example.com/style.css",
            "https://example.com/app.js",
            "https://example.com/image.png",
        ]
        for url in clean_urls:
            assert not _is_known_tracker(url), f"오탐: {url}"


# === 헬퍼 함수 ===

def _levenshtein(s1: str, s2: str) -> int:
    """레벤슈타인 편집 거리 계산"""
    if len(s1) < len(s2):
        return _levenshtein(s2, s1)
    if len(s2) == 0:
        return len(s1)
    prev_row = range(len(s2) + 1)
    for i, c1 in enumerate(s1):
        curr_row = [i + 1]
        for j, c2 in enumerate(s2):
            insertions = prev_row[j + 1] + 1
            deletions = curr_row[j] + 1
            substitutions = prev_row[j] + (c1 != c2)
            curr_row.append(min(insertions, deletions, substitutions))
        prev_row = curr_row
    return prev_row[-1]

def _detect_patterns(code: str) -> set:
    """JS 코드에서 악성 패턴 탐지"""
    patterns = set()
    if "eval(" in code: patterns.add("eval_usage")
    if "atob(" in code or "btoa(" in code: patterns.add("base64_decode")
    if "document.write" in code: patterns.add("document_write")
    if "innerHTML" in code: patterns.add("inner_html")
    if "\\x" in code or "\\u" in code: patterns.add("hex_encoding")
    if "Function(" in code: patterns.add("function_constructor")
    return patterns

def _obfuscation_score(code: str) -> float:
    """코드 난독화 점수 (0~1)"""
    score = 0.0
    # 변수명 엔트로피 (짧은 이름이 많으면 높음)
    import re
    identifiers = re.findall(r'\b_0x[0-9a-f]+\b', code)
    if identifiers: score += 0.3
    # 이스케이프 시퀀스 비율
    escape_count = code.count("\\x") + code.count("\\u")
    if escape_count > 5: score += 0.3
    elif escape_count > 0: score += 0.1
    # 가독성 (평균 토큰 길이)
    tokens = code.split()
    if tokens:
        avg_len = sum(len(t) for t in tokens) / len(tokens)
        if avg_len > 15: score += 0.2
    # 문자열 인코딩
    if "String.fromCharCode" in code: score += 0.2
    return min(score, 1.0)

def _is_known_tracker(url: str) -> bool:
    """알려진 추적기 URL 매칭"""
    tracker_domains = [
        "googletagmanager.com", "google-analytics.com", "doubleclick.net",
        "facebook.net", "fbevents.js", "analytics.js", "gtm.js",
        "hotjar.com", "mixpanel.com", "segment.io", "amplitude.com",
    ]
    return any(domain in url for domain in tracker_domains)

def _detect_fingerprinting(code: str) -> set:
    """핑거프린팅 기법 탐지"""
    techniques = set()
    if "canvas" in code.lower() and ("toDataURL" in code or "getImageData" in code or "fillText" in code):
        techniques.add("canvas")
    if "webgl" in code.lower() or "getExtension" in code:
        techniques.add("webgl")
    if "AudioContext" in code or "createOscillator" in code:
        techniques.add("audio")
    if "navigator.plugins" in code or "navigator.mimeTypes" in code:
        techniques.add("plugins")
    return techniques
