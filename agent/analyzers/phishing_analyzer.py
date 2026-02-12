"""
피싱 분석기
===========

URL 특징 추출, 콘텐츠 분석, LLM 분류를 통한 피싱 사이트 탐지.

특징 추출 항목:
- URL: 엔트로피, 길이, 서브도메인 수, IP 주소 사용, 의심 TLD, 호모글리프
- 콘텐츠: 로그인 폼, 비밀번호 필드, 외부 리소스 비율, 파비콘 불일치

점수 범위: 0.0 (안전) ~ 1.0 (확실한 피싱)
"""

from __future__ import annotations

import logging
import math
import re
from collections import Counter
from typing import Optional
from urllib.parse import urlparse

from agent.core.config import AgentConfig
from agent.core.agent import ThreatDetail, ThreatLevel, ThreatType

logger = logging.getLogger(__name__)

# ============================================================
# 호모글리프 매핑 (유니코드 유사 문자)
# ============================================================

HOMOGLYPH_MAP: dict[str, str] = {
    "а": "a",  # 키릴 문자
    "е": "e",
    "о": "o",
    "р": "p",
    "с": "c",
    "у": "y",
    "х": "x",
    "ѕ": "s",
    "і": "i",
    "ј": "j",
    "ԁ": "d",
    "ɡ": "g",
    "ɩ": "l",
    "ո": "n",
    "ν": "v",  # 그리스 문자
    "τ": "t",
    "ω": "w",
    "ℓ": "l",
    "０": "0",  # 전각 문자
    "１": "1",
    "ⅰ": "i",
}

# ============================================================
# 의심스러운 TLD 목록
# ============================================================

SUSPICIOUS_TLDS: set[str] = {
    ".tk", ".ml", ".ga", ".cf", ".gq",  # 무료 도메인
    ".top", ".xyz", ".club", ".work", ".buzz",
    ".icu", ".cam", ".rest", ".surf", ".monster",
    ".uno", ".click", ".link", ".info", ".pw",
    ".cc", ".ws", ".ru", ".cn",
}

# ============================================================
# 유명 도메인 (타이포스쿼팅 탐지용)
# ============================================================

FAMOUS_DOMAINS: list[str] = [
    "google.com", "facebook.com", "amazon.com", "apple.com",
    "microsoft.com", "netflix.com", "paypal.com", "instagram.com",
    "twitter.com", "linkedin.com", "github.com", "yahoo.com",
    "dropbox.com", "chase.com", "wellsfargo.com", "bankofamerica.com",
    "citibank.com", "usbank.com", "ebay.com", "walmart.com",
    "naver.com", "kakao.com", "daum.net", "samsung.com",
    "coinbase.com", "binance.com", "blockchain.com",
    "icloud.com", "outlook.com", "gmail.com",
]

# ============================================================
# 피싱 키워드
# ============================================================

PHISHING_KEYWORDS: list[str] = [
    "verify your account", "confirm your identity", "update your payment",
    "suspended", "unusual activity", "unauthorized", "limited access",
    "click here immediately", "act now", "urgent action",
    "계정 확인", "비밀번호 재설정", "본인 인증", "결제 정보 업데이트",
    "긴급", "계정 정지", "보안 경고",
]


class PhishingAnalyzer:
    """
    피싱 사이트 탐지 분석기

    URL 특징, 콘텐츠 특징, LLM chain-of-thought 분류를 결합하여
    0.0~1.0 범위의 피싱 점수를 산출합니다.
    """

    def __init__(self, config: AgentConfig) -> None:
        """
        피싱 분석기 초기화

        Args:
            config: 에이전트 전역 설정
        """
        self.config = config
        self._threshold = config.threats.phishing_threshold
        # IP 주소 패턴 (IPv4)
        self._ip_pattern = re.compile(
            r"^(\d{1,3}\.){3}\d{1,3}$"
        )
        # URL 내 인코딩된 문자 패턴
        self._encoded_pattern = re.compile(r"%[0-9a-fA-F]{2}")
        # 로그인 폼 패턴
        self._login_form_pattern = re.compile(
            r'<form[^>]*(?:login|signin|log-in|sign-in|auth)[^>]*>',
            re.IGNORECASE,
        )
        # 비밀번호 필드 패턴
        self._password_field_pattern = re.compile(
            r'<input[^>]*type\s*=\s*["\']password["\'][^>]*>',
            re.IGNORECASE,
        )
        # 외부 리소스 패턴 (src/href 속성)
        self._resource_pattern = re.compile(
            r'(?:src|href)\s*=\s*["\']?(https?://[^"\'\s>]+)',
            re.IGNORECASE,
        )
        # 파비콘 패턴
        self._favicon_pattern = re.compile(
            r'<link[^>]*rel\s*=\s*["\'](?:shortcut\s+)?icon["\'][^>]*'
            r'href\s*=\s*["\']?(https?://[^"\'\s>]+)',
            re.IGNORECASE,
        )
        logger.debug("PhishingAnalyzer 초기화 완료")

    # ============================
    # 공개 분석 메서드
    # ============================

    async def analyze_url(self, url: str) -> Optional[ThreatDetail]:
        """
        URL 기반 피싱 분석

        URL의 구조적 특징만으로 피싱 가능성을 평가합니다.

        Args:
            url: 분석할 URL

        Returns:
            ThreatDetail 또는 None (안전한 경우)
        """
        features = self._extract_url_features(url)
        score = self._calculate_url_score(features)

        if score < self._threshold:
            return None

        # 위협 수준 결정
        threat_level = self._score_to_level(score)

        indicators = self._collect_url_indicators(features)

        return ThreatDetail(
            threat_type=ThreatType.PHISHING,
            threat_level=threat_level,
            confidence=score,
            description=f"URL 구조 기반 피싱 의심 (점수: {score:.2f})",
            indicators=indicators,
            metadata={
                "url_features": features,
                "analysis_method": "url_heuristic",
            },
        )

    async def analyze_content(
        self, url: str, html_content: str
    ) -> Optional[ThreatDetail]:
        """
        페이지 콘텐츠 기반 피싱 분석

        HTML 내용을 분석하여 피싱 페이지의 특징을 찾습니다.

        Args:
            url: 페이지 URL
            html_content: HTML 소스 코드

        Returns:
            ThreatDetail 또는 None (안전한 경우)
        """
        content_features = self._extract_content_features(url, html_content)
        score = self._calculate_content_score(content_features)

        if score < self._threshold:
            return None

        threat_level = self._score_to_level(score)
        indicators = self._collect_content_indicators(content_features)

        return ThreatDetail(
            threat_type=ThreatType.PHISHING,
            threat_level=threat_level,
            confidence=score,
            description=f"콘텐츠 기반 피싱 의심 (점수: {score:.2f})",
            indicators=indicators,
            metadata={
                "content_features": content_features,
                "analysis_method": "content_heuristic",
            },
        )

    # ============================
    # URL 특징 추출
    # ============================

    def _extract_url_features(self, url: str) -> dict:
        """
        URL에서 피싱 판별에 필요한 특징들을 추출

        40개 이상의 특징을 추출하여 딕셔너리로 반환합니다.
        """
        parsed = urlparse(url)
        domain = parsed.hostname or ""
        path = parsed.path or ""

        # 기본 특징
        features: dict = {
            "url": url,
            "domain": domain,
            "scheme": parsed.scheme,
            "path": path,
            "length": len(url),
            "domain_length": len(domain),
            "path_length": len(path),
        }

        # 엔트로피 계산
        features["entropy"] = self._calculate_entropy(url)
        features["domain_entropy"] = self._calculate_entropy(domain)

        # 서브도메인 분석
        domain_parts = domain.split(".")
        features["subdomain_count"] = max(0, len(domain_parts) - 2)
        features["subdomain_depth"] = features["subdomain_count"]

        # IP 주소 사용 여부
        features["is_ip_address"] = bool(self._ip_pattern.match(domain))

        # HTTPS 여부
        features["is_https"] = parsed.scheme == "https"

        # 포트 사용 여부
        features["has_port"] = parsed.port is not None and parsed.port not in (80, 443)
        features["port"] = parsed.port

        # 특수 문자 비율
        special_chars = sum(1 for c in url if not c.isalnum() and c not in ":/.-_")
        features["special_char_count"] = special_chars
        features["special_char_ratio"] = special_chars / max(len(url), 1)

        # 숫자 비율
        digit_count = sum(1 for c in domain if c.isdigit())
        features["digit_count"] = digit_count
        features["digit_ratio"] = digit_count / max(len(domain), 1)

        # @ 기호 사용 (URL 위조에 사용)
        features["has_at_symbol"] = "@" in url

        # 하이픈 분석
        features["hyphen_count"] = domain.count("-")
        features["has_double_hyphen"] = "--" in domain

        # 인코딩된 문자 수
        encoded_matches = self._encoded_pattern.findall(url)
        features["encoded_char_count"] = len(encoded_matches)

        # TLD 분석
        tld = "." + domain_parts[-1] if domain_parts else ""
        features["tld"] = tld
        features["is_suspicious_tld"] = tld.lower() in SUSPICIOUS_TLDS

        # 경로 분석
        features["path_depth"] = path.count("/") - 1 if path else 0
        features["query_param_count"] = len(parsed.query.split("&")) if parsed.query else 0
        features["has_fragment"] = bool(parsed.fragment)

        # 호모글리프 탐지
        homoglyph_result = self._detect_homoglyphs(domain)
        features["has_homoglyphs"] = homoglyph_result["detected"]
        features["homoglyph_chars"] = homoglyph_result["chars"]
        features["normalized_domain"] = homoglyph_result["normalized"]

        # 타이포스쿼팅 탐지
        typosquat_result = self._detect_typosquatting(domain)
        features["is_typosquatting"] = typosquat_result["detected"]
        features["similar_domain"] = typosquat_result["similar_to"]
        features["domain_similarity"] = typosquat_result["similarity"]

        # 키워드 포함 여부 (도메인에 브랜드명 포함)
        features["contains_brand_name"] = self._contains_brand_name(domain)

        return features

    # ============================
    # 콘텐츠 특징 추출
    # ============================

    def _extract_content_features(self, url: str, html: str) -> dict:
        """HTML 콘텐츠에서 피싱 관련 특징 추출"""
        parsed = urlparse(url)
        page_domain = parsed.hostname or ""

        features: dict = {
            "url": url,
            "page_domain": page_domain,
            "html_length": len(html),
        }

        # 로그인 폼 탐지
        login_forms = self._login_form_pattern.findall(html)
        features["login_form_count"] = len(login_forms)

        # 비밀번호 필드 수
        password_fields = self._password_field_pattern.findall(html)
        features["password_field_count"] = len(password_fields)

        # 외부 리소스 분석
        all_resources = self._resource_pattern.findall(html)
        external_resources = [
            r for r in all_resources
            if page_domain and page_domain not in r
        ]
        features["total_resource_count"] = len(all_resources)
        features["external_resource_count"] = len(external_resources)
        features["external_resource_ratio"] = (
            len(external_resources) / max(len(all_resources), 1)
        )

        # 파비콘 불일치 검사
        favicon_matches = self._favicon_pattern.findall(html)
        features["has_favicon"] = len(favicon_matches) > 0
        features["favicon_mismatch"] = any(
            page_domain and page_domain not in fav
            for fav in favicon_matches
        )

        # iframe 수
        iframe_count = len(re.findall(r"<iframe", html, re.IGNORECASE))
        features["iframe_count"] = iframe_count

        # 숨겨진 요소 수
        hidden_elements = len(re.findall(
            r'(?:display\s*:\s*none|visibility\s*:\s*hidden|'
            r'type\s*=\s*["\']hidden["\'])',
            html, re.IGNORECASE,
        ))
        features["hidden_element_count"] = hidden_elements

        # 피싱 키워드 매칭
        html_lower = html.lower()
        matched_keywords = [
            kw for kw in PHISHING_KEYWORDS
            if kw.lower() in html_lower
        ]
        features["phishing_keyword_count"] = len(matched_keywords)
        features["phishing_keywords_found"] = matched_keywords

        # data: URI 사용 (피싱에서 자주 사용)
        data_uri_count = len(re.findall(r'src\s*=\s*["\']data:', html, re.IGNORECASE))
        features["data_uri_count"] = data_uri_count

        # 자동 제출 폼
        auto_submit = len(re.findall(
            r'(?:onload|setTimeout|setInterval)[^>]*submit',
            html, re.IGNORECASE,
        ))
        features["auto_submit_count"] = auto_submit

        # 우클릭 차단 (피싱 사이트 특징)
        features["disables_right_click"] = bool(re.search(
            r'oncontextmenu\s*=\s*["\']?\s*(?:return\s+false|event\.preventDefault)',
            html, re.IGNORECASE,
        ))

        return features

    # ============================
    # 점수 계산
    # ============================

    def _calculate_url_score(self, features: dict) -> float:
        """URL 특징 기반 피싱 점수 계산 (0.0 ~ 1.0)"""
        score = 0.0
        weights: list[tuple[str, float]] = []

        # IP 주소 사용 (+0.3)
        if features.get("is_ip_address"):
            weights.append(("ip_address", 0.30))

        # HTTPS 미사용 (+0.1)
        if not features.get("is_https"):
            weights.append(("no_https", 0.10))

        # 의심스러운 TLD (+0.15)
        if features.get("is_suspicious_tld"):
            weights.append(("suspicious_tld", 0.15))

        # 과도한 서브도메인 (3개 이상 +0.15)
        subdomain_count = features.get("subdomain_count", 0)
        if subdomain_count >= 3:
            weights.append(("many_subdomains", 0.15))
        elif subdomain_count >= 2:
            weights.append(("some_subdomains", 0.08))

        # URL 길이 (75자 이상 +0.1)
        url_length = features.get("length", 0)
        if url_length > 100:
            weights.append(("very_long_url", 0.12))
        elif url_length > 75:
            weights.append(("long_url", 0.08))

        # 높은 엔트로피 (4.5 이상 +0.1)
        entropy = features.get("entropy", 0.0)
        if entropy > 5.0:
            weights.append(("very_high_entropy", 0.12))
        elif entropy > 4.5:
            weights.append(("high_entropy", 0.08))

        # @ 기호 사용 (+0.2)
        if features.get("has_at_symbol"):
            weights.append(("at_symbol", 0.20))

        # 비정상 포트 (+0.15)
        if features.get("has_port"):
            weights.append(("unusual_port", 0.15))

        # 호모글리프 탐지 (+0.3)
        if features.get("has_homoglyphs"):
            weights.append(("homoglyphs", 0.30))

        # 타이포스쿼팅 탐지 (+0.35)
        if features.get("is_typosquatting"):
            similarity = features.get("domain_similarity", 0.0)
            weights.append(("typosquatting", 0.25 + similarity * 0.10))

        # 도메인에 브랜드명 포함 (+0.15)
        if features.get("contains_brand_name"):
            weights.append(("brand_in_domain", 0.15))

        # 높은 숫자 비율 (+0.08)
        if features.get("digit_ratio", 0.0) > 0.3:
            weights.append(("high_digit_ratio", 0.08))

        # 높은 특수문자 비율 (+0.08)
        if features.get("special_char_ratio", 0.0) > 0.2:
            weights.append(("high_special_char_ratio", 0.08))

        # 인코딩된 문자 많음 (+0.1)
        if features.get("encoded_char_count", 0) > 3:
            weights.append(("many_encoded_chars", 0.10))

        # 더블 하이픈 (+0.05)
        if features.get("has_double_hyphen"):
            weights.append(("double_hyphen", 0.05))

        # 깊은 경로 (+0.05)
        if features.get("path_depth", 0) > 5:
            weights.append(("deep_path", 0.05))

        # 점수 합산 (최대 1.0)
        score = sum(w for _, w in weights)
        return min(1.0, max(0.0, score))

    def _calculate_content_score(self, features: dict) -> float:
        """콘텐츠 특징 기반 피싱 점수 계산 (0.0 ~ 1.0)"""
        score = 0.0
        weights: list[tuple[str, float]] = []

        # 로그인 폼 존재 (+0.15)
        if features.get("login_form_count", 0) > 0:
            weights.append(("login_form", 0.15))

        # 비밀번호 필드 존재 (+0.15)
        if features.get("password_field_count", 0) > 0:
            weights.append(("password_field", 0.15))

        # 높은 외부 리소스 비율 (+0.15)
        ext_ratio = features.get("external_resource_ratio", 0.0)
        if ext_ratio > 0.8:
            weights.append(("high_external_ratio", 0.15))
        elif ext_ratio > 0.5:
            weights.append(("medium_external_ratio", 0.08))

        # 파비콘 불일치 (+0.2)
        if features.get("favicon_mismatch"):
            weights.append(("favicon_mismatch", 0.20))

        # 피싱 키워드 매칭 (+0.05 per keyword, 최대 0.2)
        keyword_count = features.get("phishing_keyword_count", 0)
        if keyword_count > 0:
            keyword_score = min(0.20, keyword_count * 0.05)
            weights.append(("phishing_keywords", keyword_score))

        # iframe 사용 (+0.1)
        if features.get("iframe_count", 0) > 0:
            weights.append(("iframe_usage", 0.10))

        # 숨겨진 요소 (+0.1)
        if features.get("hidden_element_count", 0) > 3:
            weights.append(("hidden_elements", 0.10))

        # data: URI 사용 (+0.1)
        if features.get("data_uri_count", 0) > 0:
            weights.append(("data_uri", 0.10))

        # 자동 폼 제출 (+0.2)
        if features.get("auto_submit_count", 0) > 0:
            weights.append(("auto_submit", 0.20))

        # 우클릭 차단 (+0.1)
        if features.get("disables_right_click"):
            weights.append(("no_right_click", 0.10))

        score = sum(w for _, w in weights)
        return min(1.0, max(0.0, score))

    # ============================
    # 호모글리프 탐지
    # ============================

    def _detect_homoglyphs(self, domain: str) -> dict:
        """
        도메인에서 유니코드 호모글리프 문자 탐지

        키릴, 그리스, 전각 문자 등 라틴 문자와 유사한 문자를 감지합니다.
        """
        detected_chars: list[str] = []
        normalized = ""

        for char in domain:
            if char in HOMOGLYPH_MAP:
                detected_chars.append(char)
                normalized += HOMOGLYPH_MAP[char]
            else:
                normalized += char

        return {
            "detected": len(detected_chars) > 0,
            "chars": detected_chars,
            "normalized": normalized,
        }

    # ============================
    # 타이포스쿼팅 탐지
    # ============================

    def _detect_typosquatting(self, domain: str) -> dict:
        """
        유명 도메인과의 편집 거리 기반 타이포스쿼팅 탐지

        레벤슈타인 거리를 사용하여 유명 도메인과의 유사도를 계산합니다.
        """
        best_match = ""
        best_similarity = 0.0

        # 도메인에서 TLD 제거
        domain_parts = domain.split(".")
        if len(domain_parts) >= 2:
            domain_base = ".".join(domain_parts[-(2 if len(domain_parts) >= 2 else 1):])
        else:
            domain_base = domain

        for famous in FAMOUS_DOMAINS:
            if domain_base == famous:
                # 정확히 일치하면 안전
                return {"detected": False, "similar_to": "", "similarity": 0.0}

            distance = self._levenshtein_distance(domain_base, famous)
            max_len = max(len(domain_base), len(famous))
            similarity = 1.0 - (distance / max_len) if max_len > 0 else 0.0

            if similarity > best_similarity:
                best_similarity = similarity
                best_match = famous

        # 유사도 80% 이상이고 정확히 일치하지 않으면 타이포스쿼팅
        is_typosquat = best_similarity >= 0.80 and best_match != domain_base

        return {
            "detected": is_typosquat,
            "similar_to": best_match if is_typosquat else "",
            "similarity": best_similarity if is_typosquat else 0.0,
        }

    def _levenshtein_distance(self, s1: str, s2: str) -> int:
        """레벤슈타인 편집 거리 계산"""
        if len(s1) < len(s2):
            return self._levenshtein_distance(s2, s1)

        if len(s2) == 0:
            return len(s1)

        prev_row = list(range(len(s2) + 1))

        for i, c1 in enumerate(s1):
            curr_row = [i + 1]
            for j, c2 in enumerate(s2):
                # 삽입, 삭제, 치환 비용 계산
                insertions = prev_row[j + 1] + 1
                deletions = curr_row[j] + 1
                substitutions = prev_row[j] + (0 if c1 == c2 else 1)
                curr_row.append(min(insertions, deletions, substitutions))
            prev_row = curr_row

        return prev_row[-1]

    # ============================
    # 엔트로피 계산
    # ============================

    def _calculate_entropy(self, text: str) -> float:
        """
        Shannon 엔트로피 계산

        문자열의 무작위성을 측정합니다. 높은 엔트로피는
        랜덤 문자열(예: 해시, 인코딩된 데이터)을 나타냅니다.
        """
        if not text:
            return 0.0

        counter = Counter(text)
        length = len(text)
        entropy = 0.0

        for count in counter.values():
            probability = count / length
            if probability > 0:
                entropy -= probability * math.log2(probability)

        return entropy

    # ============================
    # 브랜드명 탐지
    # ============================

    def _contains_brand_name(self, domain: str) -> bool:
        """도메인 내 유명 브랜드명 포함 여부 확인"""
        brand_names = [
            "google", "facebook", "amazon", "apple", "microsoft",
            "netflix", "paypal", "instagram", "twitter", "linkedin",
            "github", "yahoo", "dropbox", "chase", "wells",
            "citibank", "ebay", "walmart", "naver", "kakao",
            "samsung", "coinbase", "binance", "icloud",
        ]
        domain_lower = domain.lower()

        for brand in brand_names:
            # 브랜드명이 도메인에 포함되지만, 공식 도메인이 아닌 경우
            if brand in domain_lower:
                # 공식 도메인 확인
                official = f"{brand}.com"
                if domain_lower != official and not domain_lower.endswith(f".{official}"):
                    return True
        return False

    # ============================
    # 지표 수집
    # ============================

    def _collect_url_indicators(self, features: dict) -> list[str]:
        """URL 특징에서 피싱 지표 목록 수집"""
        indicators: list[str] = []

        if features.get("is_ip_address"):
            indicators.append("IP 주소를 도메인으로 사용")
        if not features.get("is_https"):
            indicators.append("HTTPS 미사용")
        if features.get("is_suspicious_tld"):
            indicators.append(f"의심스러운 TLD: {features.get('tld')}")
        if features.get("subdomain_count", 0) >= 3:
            indicators.append(f"과도한 서브도메인: {features['subdomain_count']}개")
        if features.get("has_at_symbol"):
            indicators.append("URL에 @ 기호 포함 (URL 위조 가능)")
        if features.get("has_homoglyphs"):
            indicators.append(
                f"호모글리프 문자 탐지: {features.get('homoglyph_chars')}"
            )
        if features.get("is_typosquatting"):
            indicators.append(
                f"타이포스쿼팅 의심: '{features.get('similar_domain')}'과 유사"
            )
        if features.get("contains_brand_name"):
            indicators.append("도메인에 유명 브랜드명 포함")
        if features.get("has_port"):
            indicators.append(f"비정상 포트 사용: {features.get('port')}")
        if features.get("length", 0) > 75:
            indicators.append(f"비정상적으로 긴 URL: {features['length']}자")
        if features.get("entropy", 0) > 4.5:
            indicators.append(f"높은 URL 엔트로피: {features['entropy']:.2f}")

        return indicators

    def _collect_content_indicators(self, features: dict) -> list[str]:
        """콘텐츠 특징에서 피싱 지표 목록 수집"""
        indicators: list[str] = []

        if features.get("login_form_count", 0) > 0:
            indicators.append("로그인 폼 감지")
        if features.get("password_field_count", 0) > 0:
            indicators.append(f"비밀번호 필드 {features['password_field_count']}개")
        if features.get("favicon_mismatch"):
            indicators.append("파비콘 출처 불일치")
        if features.get("phishing_keyword_count", 0) > 0:
            keywords = features.get("phishing_keywords_found", [])
            indicators.append(f"피싱 키워드 탐지: {', '.join(keywords[:3])}")
        if features.get("auto_submit_count", 0) > 0:
            indicators.append("자동 폼 제출 감지")
        if features.get("disables_right_click"):
            indicators.append("우클릭 차단 (피싱 특징)")
        if features.get("external_resource_ratio", 0) > 0.8:
            indicators.append("외부 리소스 비율 과다")
        if features.get("iframe_count", 0) > 0:
            indicators.append(f"iframe {features['iframe_count']}개 사용")

        return indicators

    # ============================
    # 유틸리티
    # ============================

    @staticmethod
    def _score_to_level(score: float) -> ThreatLevel:
        """점수를 위협 수준으로 변환"""
        if score >= 0.9:
            return ThreatLevel.CRITICAL
        elif score >= 0.75:
            return ThreatLevel.HIGH
        elif score >= 0.5:
            return ThreatLevel.MEDIUM
        elif score >= 0.3:
            return ThreatLevel.LOW
        return ThreatLevel.SAFE
