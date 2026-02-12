"""
특징 추출기
===========

URL, DOM, JavaScript 코드에서 보안 분석용 특징 벡터를 추출합니다.

클래스:
- URLFeatureExtractor: 40+ URL 특징 (길이, 엔트로피, 도메인, 특수문자 등)
- DOMFeatureExtractor: HTML DOM 특징 (폼, 스크립트, 외부 리소스 등)
- JSFeatureExtractor: JavaScript 코드 특징 (eval, 난독화, API 호출 등)

모든 추출기는 numpy 배열을 반환합니다.
"""

from __future__ import annotations

import math
import re
from collections import Counter
from typing import Any, Optional
from urllib.parse import urlparse, parse_qs

import numpy as np


# ============================================================
# 의심스러운 TLD 및 단축 서비스 목록
# ============================================================

SUSPICIOUS_TLDS: set[str] = {
    "tk", "ml", "ga", "cf", "gq", "top", "xyz", "club",
    "work", "buzz", "icu", "cam", "rest", "surf", "monster",
    "uno", "click", "link", "info", "pw", "cc", "ws",
}

URL_SHORTENERS: set[str] = {
    "bit.ly", "tinyurl.com", "t.co", "goo.gl", "ow.ly",
    "is.gd", "buff.ly", "adf.ly", "bit.do", "mcaf.ee",
    "rebrand.ly", "cutt.ly", "shorte.st", "linktr.ee",
    "rb.gy", "shorturl.at", "tiny.cc", "v.gd", "qr.ae",
}


# ============================================================
# URL 특징 추출기
# ============================================================

class URLFeatureExtractor:
    """
    URL 특징 추출기

    40개 이상의 정량적 URL 특징을 추출하여 numpy 배열로 반환합니다.
    피싱 탐지, 악성 URL 분류 등 ML 모델 입력으로 사용됩니다.
    """

    # IP 주소 패턴 (IPv4)
    _IP_PATTERN = re.compile(r"^(\d{1,3}\.){3}\d{1,3}$")

    # 특징 이름 목록 (순서 고정)
    FEATURE_NAMES: list[str] = [
        "url_length",                  # 0: URL 전체 길이
        "domain_length",               # 1: 도메인 길이
        "path_length",                 # 2: 경로 길이
        "num_dots",                    # 3: 점(.) 개수
        "num_hyphens",                 # 4: 하이픈(-) 개수
        "num_underscores",             # 5: 밑줄(_) 개수
        "num_slashes",                 # 6: 슬래시(/) 개수
        "num_digits",                  # 7: 숫자 개수
        "digit_ratio",                 # 8: 숫자 비율
        "letter_ratio",                # 9: 문자 비율
        "special_char_ratio",          # 10: 특수문자 비율
        "has_ip",                      # 11: IP 주소 사용 여부
        "has_port",                    # 12: 포트 사용 여부
        "num_subdomains",              # 13: 서브도메인 수
        "tld_length",                  # 14: TLD 길이
        "is_https",                    # 15: HTTPS 여부
        "entropy",                     # 16: URL 엔트로피
        "longest_word",                # 17: 가장 긴 단어 길이
        "avg_word_length",             # 18: 평균 단어 길이
        "num_params",                  # 19: 쿼리 파라미터 수
        "param_length",                # 20: 파라미터 전체 길이
        "has_at_symbol",               # 21: @ 기호 포함 여부
        "has_double_slash_redirect",   # 22: // 리다이렉트 패턴
        "prefix_suffix_in_domain",     # 23: 도메인 내 하이픈(접두-접미)
        "shortening_service",          # 24: URL 단축 서비스 여부
        "suspicious_tld_score",        # 25: 의심스러운 TLD 점수
        "domain_entropy",              # 26: 도메인 엔트로피
        "path_entropy",                # 27: 경로 엔트로피
        "num_fragments",               # 28: 프래그먼트 존재 여부
        "num_percent_encoded",         # 29: 퍼센트 인코딩 수
        "num_at_symbols",              # 30: @ 기호 수
        "num_ampersands",              # 31: & 기호 수
        "num_equals",                  # 32: = 기호 수
        "num_tildes",                  # 33: ~ 기호 수
        "num_semicolons",              # 34: 세미콜론 수
        "has_double_hyphen",           # 35: 더블 하이픈 여부
        "query_length",                # 36: 쿼리 문자열 길이
        "subdomain_length",            # 37: 서브도메인 길이
        "domain_token_count",          # 38: 도메인 토큰 수
        "path_token_count",            # 39: 경로 토큰 수
        "max_domain_token_length",     # 40: 최대 도메인 토큰 길이
        "avg_domain_token_length",     # 41: 평균 도메인 토큰 길이
    ]

    def extract(self, url: str) -> dict[str, Any]:
        """
        URL에서 특징 딕셔너리 추출

        Args:
            url: 분석할 URL

        Returns:
            특징명 → 값 딕셔너리 (numpy 배열 포함)
        """
        features = self._extract_raw(url)
        features["feature_vector"] = self.extract_vector(url)
        return features

    def extract_vector(self, url: str) -> np.ndarray:
        """
        URL에서 특징 벡터(numpy 배열) 추출

        Args:
            url: 분석할 URL

        Returns:
            float32 numpy 배열 (shape: [42])
        """
        features = self._extract_raw(url)
        vector = np.array(
            [features[name] for name in self.FEATURE_NAMES],
            dtype=np.float32,
        )
        return vector

    def _extract_raw(self, url: str) -> dict[str, Any]:
        """URL에서 원시 특징 딕셔너리 추출"""
        # URL 파싱
        if not url.startswith(("http://", "https://", "ftp://")):
            url = "https://" + url

        parsed = urlparse(url)
        domain = parsed.hostname or ""
        path = parsed.path or ""
        query = parsed.query or ""
        fragment = parsed.fragment or ""

        # 도메인 토큰 분할
        domain_parts = domain.split(".") if domain else []
        # 경로 토큰 분할
        path_tokens = [t for t in path.split("/") if t]
        # 쿼리 파라미터
        params = parse_qs(query)

        # 서브도메인 부분 (TLD와 SLD 제외)
        subdomain = ".".join(domain_parts[:-2]) if len(domain_parts) > 2 else ""
        tld = domain_parts[-1] if domain_parts else ""

        # 숫자/문자/특수문자 카운트
        num_digits = sum(1 for c in url if c.isdigit())
        num_letters = sum(1 for c in url if c.isalpha())
        num_special = sum(1 for c in url if not c.isalnum() and c not in ":/.-_")
        url_len = max(len(url), 1)

        # 단어 분할 (영숫자 토큰)
        words = re.findall(r'[a-zA-Z0-9]+', url)
        word_lengths = [len(w) for w in words] if words else [0]

        # 퍼센트 인코딩 수
        percent_encoded = len(re.findall(r'%[0-9a-fA-F]{2}', url))

        features: dict[str, Any] = {
            # 기본 길이 특징
            "url_length": len(url),
            "domain_length": len(domain),
            "path_length": len(path),

            # 문자 개수 특징
            "num_dots": url.count("."),
            "num_hyphens": url.count("-"),
            "num_underscores": url.count("_"),
            "num_slashes": url.count("/"),
            "num_digits": num_digits,

            # 비율 특징
            "digit_ratio": num_digits / url_len,
            "letter_ratio": num_letters / url_len,
            "special_char_ratio": num_special / url_len,

            # 불리언 특징 (0/1)
            "has_ip": 1.0 if self._IP_PATTERN.match(domain) else 0.0,
            "has_port": 1.0 if (parsed.port and parsed.port not in (80, 443)) else 0.0,

            # 도메인 구조 특징
            "num_subdomains": max(0, len(domain_parts) - 2),
            "tld_length": len(tld),
            "is_https": 1.0 if parsed.scheme == "https" else 0.0,

            # 엔트로피 특징
            "entropy": self._shannon_entropy(url),
            "domain_entropy": self._shannon_entropy(domain),
            "path_entropy": self._shannon_entropy(path),

            # 단어 특징
            "longest_word": max(word_lengths),
            "avg_word_length": sum(word_lengths) / max(len(word_lengths), 1),

            # 쿼리 특징
            "num_params": len(params),
            "param_length": len(query),

            # 패턴 특징
            "has_at_symbol": 1.0 if "@" in url else 0.0,
            "has_double_slash_redirect": 1.0 if "//" in path else 0.0,
            "prefix_suffix_in_domain": 1.0 if "-" in domain else 0.0,

            # 서비스 특징
            "shortening_service": 1.0 if domain in URL_SHORTENERS else 0.0,
            "suspicious_tld_score": 1.0 if tld.lower() in SUSPICIOUS_TLDS else 0.0,

            # 프래그먼트 / 인코딩 특징
            "num_fragments": 1.0 if fragment else 0.0,
            "num_percent_encoded": float(percent_encoded),

            # 추가 특수문자 카운트
            "num_at_symbols": float(url.count("@")),
            "num_ampersands": float(url.count("&")),
            "num_equals": float(url.count("=")),
            "num_tildes": float(url.count("~")),
            "num_semicolons": float(url.count(";")),

            # 추가 패턴 특징
            "has_double_hyphen": 1.0 if "--" in domain else 0.0,
            "query_length": float(len(query)),
            "subdomain_length": float(len(subdomain)),

            # 도메인/경로 토큰 특징
            "domain_token_count": float(len(domain_parts)),
            "path_token_count": float(len(path_tokens)),
            "max_domain_token_length": float(
                max((len(p) for p in domain_parts), default=0)
            ),
            "avg_domain_token_length": float(
                sum(len(p) for p in domain_parts) / max(len(domain_parts), 1)
            ),

            # 메타 정보 (벡터에는 포함 안 됨)
            "domain": domain,
            "subdomain_count": max(0, len(domain_parts) - 2),
            "is_ip_address": bool(self._IP_PATTERN.match(domain)),
            "length": len(url),
        }

        return features

    @staticmethod
    def _shannon_entropy(text: str) -> float:
        """Shannon 엔트로피 계산"""
        if not text:
            return 0.0
        counter = Counter(text)
        length = len(text)
        return -sum(
            (c / length) * math.log2(c / length)
            for c in counter.values()
            if c > 0
        )


# ============================================================
# DOM 특징 추출기
# ============================================================

class DOMFeatureExtractor:
    """
    HTML DOM 특징 추출기

    HTML 콘텐츠에서 보안 분석용 특징을 추출합니다.
    폼, 스크립트, 외부 리소스, 숨겨진 요소 등을 분석합니다.
    """

    # 특징 이름 목록
    FEATURE_NAMES: list[str] = [
        "form_count",                  # 0: 폼 수
        "password_field_count",        # 1: 비밀번호 필드 수
        "input_field_count",           # 2: 입력 필드 수
        "external_script_count",       # 3: 외부 스크립트 수
        "inline_script_count",         # 4: 인라인 스크립트 수
        "external_resource_count",     # 5: 외부 리소스 수
        "external_resource_ratio",     # 6: 외부 리소스 비율
        "iframe_count",                # 7: iframe 수
        "hidden_element_count",        # 8: 숨겨진 요소 수
        "img_count",                   # 9: 이미지 수
        "link_count",                  # 10: 링크 수
        "meta_count",                  # 11: 메타 태그 수
        "title_length",                # 12: 제목 길이
        "html_length",                 # 13: HTML 길이
        "text_to_html_ratio",          # 14: 텍스트/HTML 비율
        "has_favicon",                 # 15: 파비콘 여부
        "has_login_form",              # 16: 로그인 폼 여부
        "has_popup",                   # 17: 팝업 코드 여부
        "has_right_click_disabled",    # 18: 우클릭 차단 여부
        "has_auto_redirect",           # 19: 자동 리다이렉트 여부
        "data_uri_count",              # 20: data: URI 수
        "event_handler_count",         # 21: 인라인 이벤트 핸들러 수
        "suspicious_keyword_count",    # 22: 의심 키워드 수
        "external_form_action",        # 23: 외부 폼 액션 여부
    ]

    # 의심 키워드 (피싱/소셜 엔지니어링)
    _SUSPICIOUS_KEYWORDS: list[str] = [
        "verify", "confirm", "update", "suspend", "restrict",
        "unauthorized", "unusual", "urgent", "immediately",
        "계정", "확인", "인증", "비밀번호", "결제", "긴급",
    ]

    def extract(self, html: str) -> dict[str, Any]:
        """
        HTML에서 특징 딕셔너리 추출

        Args:
            html: HTML 소스 코드

        Returns:
            특징명 → 값 딕셔너리 (numpy 배열 포함)
        """
        features = self._extract_raw(html)
        features["feature_vector"] = self.extract_vector(html)
        return features

    def extract_vector(self, html: str) -> np.ndarray:
        """
        HTML에서 특징 벡터(numpy 배열) 추출

        Args:
            html: HTML 소스 코드

        Returns:
            float32 numpy 배열 (shape: [24])
        """
        features = self._extract_raw(html)
        vector = np.array(
            [features[name] for name in self.FEATURE_NAMES],
            dtype=np.float32,
        )
        return vector

    def _extract_raw(self, html: str) -> dict[str, Any]:
        """HTML에서 원시 특징 추출"""
        html_lower = html.lower()

        # 폼 관련
        forms = re.findall(r'<form[^>]*>', html, re.IGNORECASE)
        password_fields = re.findall(
            r'<input[^>]*type\s*=\s*["\']password["\']', html, re.IGNORECASE
        )
        input_fields = re.findall(r'<input[^>]*>', html, re.IGNORECASE)

        # 스크립트 관련
        all_scripts = re.findall(r'<script[^>]*>(.*?)</script>', html, re.DOTALL | re.IGNORECASE)
        external_scripts = re.findall(
            r'<script[^>]*src\s*=\s*["\']', html, re.IGNORECASE
        )
        inline_scripts = len(all_scripts) - len(external_scripts)

        # 외부 리소스
        all_resources = re.findall(
            r'(?:src|href)\s*=\s*["\']?(https?://[^"\'\s>]+)', html, re.IGNORECASE
        )
        external_resources = len(all_resources)

        # 이미지, 링크, 메타, iframe
        img_count = len(re.findall(r'<img[^>]*>', html, re.IGNORECASE))
        link_count = len(re.findall(r'<a[^>]*href', html, re.IGNORECASE))
        meta_count = len(re.findall(r'<meta[^>]*>', html, re.IGNORECASE))
        iframe_count = len(re.findall(r'<iframe[^>]*>', html, re.IGNORECASE))

        # 숨겨진 요소
        hidden_count = len(re.findall(
            r'(?:display\s*:\s*none|visibility\s*:\s*hidden|'
            r'type\s*=\s*["\']hidden["\'])',
            html, re.IGNORECASE,
        ))

        # 제목 추출
        title_match = re.search(r'<title[^>]*>(.*?)</title>', html, re.DOTALL | re.IGNORECASE)
        title = title_match.group(1).strip() if title_match else ""

        # 텍스트/HTML 비율 (태그 제거 후 텍스트 길이)
        text_only = re.sub(r'<[^>]+>', '', html)
        text_only = re.sub(r'\s+', ' ', text_only).strip()
        text_ratio = len(text_only) / max(len(html), 1)

        # 파비콘 여부
        has_favicon = bool(re.search(
            r'<link[^>]*rel\s*=\s*["\'](?:shortcut\s+)?icon["\']',
            html, re.IGNORECASE,
        ))

        # 로그인 폼
        has_login = bool(re.search(
            r'<form[^>]*(?:login|signin|log-in|sign-in|auth)',
            html, re.IGNORECASE,
        ))

        # 팝업
        has_popup = bool(re.search(
            r'window\.open\s*\(', html, re.IGNORECASE
        ))

        # 우클릭 차단
        has_no_right_click = bool(re.search(
            r'oncontextmenu\s*=\s*["\']?\s*(?:return\s+false|event\.preventDefault)',
            html, re.IGNORECASE,
        ))

        # 자동 리다이렉트
        has_auto_redirect = bool(re.search(
            r'(?:meta[^>]*http-equiv\s*=\s*["\']refresh["\']|'
            r'window\.location\s*=|location\.replace)',
            html, re.IGNORECASE,
        ))

        # data: URI
        data_uris = len(re.findall(r'(?:src|href)\s*=\s*["\']data:', html, re.IGNORECASE))

        # 인라인 이벤트 핸들러
        event_handlers = len(re.findall(
            r'\bon(?:click|load|error|mouseover|focus|blur|submit|change)\s*=',
            html, re.IGNORECASE,
        ))

        # 의심 키워드
        suspicious_count = sum(
            1 for kw in self._SUSPICIOUS_KEYWORDS if kw.lower() in html_lower
        )

        # 외부 폼 액션 여부
        external_action = bool(re.search(
            r'<form[^>]*action\s*=\s*["\']https?://', html, re.IGNORECASE
        ))

        total_resources = max(
            external_resources + len(re.findall(r'(?:src|href)\s*=\s*["\'](?!/|#)', html, re.IGNORECASE)),
            1,
        )

        return {
            "form_count": float(len(forms)),
            "password_field_count": float(len(password_fields)),
            "input_field_count": float(len(input_fields)),
            "external_script_count": float(len(external_scripts)),
            "inline_script_count": float(max(inline_scripts, 0)),
            "external_resource_count": float(external_resources),
            "external_resource_ratio": external_resources / total_resources,
            "iframe_count": float(iframe_count),
            "hidden_element_count": float(hidden_count),
            "img_count": float(img_count),
            "link_count": float(link_count),
            "meta_count": float(meta_count),
            "title_length": float(len(title)),
            "html_length": float(len(html)),
            "text_to_html_ratio": text_ratio,
            "has_favicon": 1.0 if has_favicon else 0.0,
            "has_login_form": 1.0 if has_login else 0.0,
            "has_popup": 1.0 if has_popup else 0.0,
            "has_right_click_disabled": 1.0 if has_no_right_click else 0.0,
            "has_auto_redirect": 1.0 if has_auto_redirect else 0.0,
            "data_uri_count": float(data_uris),
            "event_handler_count": float(event_handlers),
            "suspicious_keyword_count": float(suspicious_count),
            "external_form_action": 1.0 if external_action else 0.0,
            # 메타 (벡터에는 미포함)
            "title": title,
        }


# ============================================================
# JavaScript 특징 추출기
# ============================================================

class JSFeatureExtractor:
    """
    JavaScript 코드 특징 추출기

    코드에서 악성 행위 지표, 난독화 수준, API 호출 패턴을 추출합니다.
    """

    # 특징 이름 목록
    FEATURE_NAMES: list[str] = [
        "code_length",                 # 0: 코드 길이
        "line_count",                  # 1: 줄 수
        "avg_line_length",             # 2: 평균 줄 길이
        "max_line_length",             # 3: 최대 줄 길이
        "eval_count",                  # 4: eval() 수
        "document_write_count",        # 5: document.write() 수
        "set_timeout_count",           # 6: setTimeout() 수
        "set_interval_count",          # 7: setInterval() 수
        "function_constructor_count",  # 8: new Function() 수
        "encoded_string_count",        # 9: 인코딩된 문자열 수
        "base64_count",                # 10: base64 패턴 수
        "hex_string_count",            # 11: \x 패턴 수
        "unicode_string_count",        # 12: \u 패턴 수
        "string_fromcharcode_count",   # 13: fromCharCode 수
        "variable_name_entropy",       # 14: 변수명 엔트로피
        "variable_count",              # 15: 변수 수
        "function_count",              # 16: 함수 수
        "obfuscation_score",           # 17: 난독화 점수
        "code_density",                # 18: 코드 밀도
        "comment_ratio",               # 19: 주석 비율
        "string_ratio",                # 20: 문자열 비율
        "dom_access_count",            # 21: DOM 접근 수
        "cookie_access_count",         # 22: 쿠키 접근 수
        "storage_access_count",        # 23: Storage 접근 수
        "xhr_fetch_count",             # 24: XHR/fetch 수
        "websocket_count",             # 25: WebSocket 수
        "crypto_api_count",            # 26: 암호화 API 수
        "redirect_count",              # 27: 리다이렉트 수
        "iframe_create_count",         # 28: iframe 생성 수
        "event_listener_count",        # 29: addEventListener 수
        "try_catch_count",             # 30: try-catch 수
        "hex_var_count",               # 31: _0x 변수 수
    ]

    def extract(self, code: str) -> dict[str, Any]:
        """
        JavaScript 코드에서 특징 딕셔너리 추출

        Args:
            code: JavaScript 소스 코드

        Returns:
            특징명 → 값 딕셔너리 (numpy 배열 포함)
        """
        features = self._extract_raw(code)
        features["feature_vector"] = self.extract_vector(code)
        return features

    def extract_vector(self, code: str) -> np.ndarray:
        """
        JavaScript 코드에서 특징 벡터(numpy 배열) 추출

        Args:
            code: JavaScript 소스 코드

        Returns:
            float32 numpy 배열 (shape: [32])
        """
        features = self._extract_raw(code)
        vector = np.array(
            [features[name] for name in self.FEATURE_NAMES],
            dtype=np.float32,
        )
        return vector

    def _extract_raw(self, code: str) -> dict[str, Any]:
        """JavaScript에서 원시 특징 추출"""
        # 기본 코드 통계
        lines = code.split("\n")
        non_empty_lines = [ln for ln in lines if ln.strip()]
        line_lengths = [len(ln) for ln in non_empty_lines] if non_empty_lines else [0]

        # 주석 추출 (단일줄, 다중줄)
        single_comments = re.findall(r'//[^\n]*', code)
        multi_comments = re.findall(r'/\*.*?\*/', code, re.DOTALL)
        comment_chars = sum(len(c) for c in single_comments) + sum(len(c) for c in multi_comments)
        comment_ratio = comment_chars / max(len(code), 1)

        # 문자열 비율
        strings = re.findall(r'(?:"[^"]*"|\'[^\']*\'|`[^`]*`)', code, re.DOTALL)
        string_chars = sum(len(s) for s in strings)
        string_ratio = string_chars / max(len(code), 1)

        # 코드 밀도 (공백 제거 후 비율)
        stripped = code.replace(" ", "").replace("\n", "").replace("\t", "")
        code_density = len(stripped) / max(len(code), 1)

        # 변수/함수 추출
        var_names = re.findall(r'\b(?:var|let|const)\s+([a-zA-Z_$][a-zA-Z0-9_$]*)', code)
        func_names = re.findall(r'\bfunction\s+([a-zA-Z_$][a-zA-Z0-9_$]*)', code)
        all_names = var_names + func_names

        # 변수명 엔트로피 계산
        var_entropy = self._names_entropy(all_names)

        # 인코딩된 문자열 수
        hex_strings = len(re.findall(r'(?:\\x[0-9a-fA-F]{2}){4,}', code))
        unicode_strings = len(re.findall(r'(?:\\u[0-9a-fA-F]{4}){3,}', code))
        base64_count = len(re.findall(r'(?:atob|btoa)\s*\(\s*["\']', code))
        fromcharcode = len(re.findall(r'String\.fromCharCode', code, re.IGNORECASE))

        # _0x 변수 수
        hex_vars = len(re.findall(r'\b_0x[a-f0-9]+\b', code))

        # 난독화 점수 계산
        obfuscation_score = self._calc_obfuscation(
            var_entropy=var_entropy,
            hex_count=hex_strings,
            unicode_count=unicode_strings,
            base64_count=base64_count,
            code_density=code_density,
            avg_line=sum(line_lengths) / max(len(line_lengths), 1),
            hex_vars=hex_vars,
        )

        return {
            # 기본 통계
            "code_length": float(len(code)),
            "line_count": float(len(non_empty_lines)),
            "avg_line_length": sum(line_lengths) / max(len(line_lengths), 1),
            "max_line_length": float(max(line_lengths)),

            # 위험 함수 호출
            "eval_count": float(len(re.findall(r'\beval\s*\(', code))),
            "document_write_count": float(len(re.findall(
                r'\bdocument\.write(?:ln)?\s*\(', code, re.IGNORECASE
            ))),
            "set_timeout_count": float(len(re.findall(r'\bsetTimeout\s*\(', code))),
            "set_interval_count": float(len(re.findall(r'\bsetInterval\s*\(', code))),
            "function_constructor_count": float(len(re.findall(
                r'\bnew\s+Function\s*\(', code, re.IGNORECASE
            ))),

            # 인코딩 패턴
            "encoded_string_count": float(hex_strings + unicode_strings + base64_count),
            "base64_count": float(base64_count),
            "hex_string_count": float(hex_strings),
            "unicode_string_count": float(unicode_strings),
            "string_fromcharcode_count": float(fromcharcode),

            # 코드 구조
            "variable_name_entropy": var_entropy,
            "variable_count": float(len(var_names)),
            "function_count": float(len(func_names)),
            "obfuscation_score": obfuscation_score,
            "code_density": code_density,
            "comment_ratio": comment_ratio,
            "string_ratio": string_ratio,

            # API 호출
            "dom_access_count": float(len(re.findall(
                r'\bdocument\.\w+', code
            ))),
            "cookie_access_count": float(len(re.findall(
                r'\bdocument\.cookie\b', code, re.IGNORECASE
            ))),
            "storage_access_count": float(len(re.findall(
                r'\b(?:localStorage|sessionStorage)\.\w+', code
            ))),
            "xhr_fetch_count": float(len(re.findall(
                r'\b(?:XMLHttpRequest|fetch\s*\(|\.ajax\s*\()', code, re.IGNORECASE
            ))),
            "websocket_count": float(len(re.findall(
                r'\bnew\s+WebSocket\s*\(', code, re.IGNORECASE
            ))),
            "crypto_api_count": float(len(re.findall(
                r'\b(?:crypto\.subtle|CryptoJS|sjcl|forge)\b', code, re.IGNORECASE
            ))),
            "redirect_count": float(len(re.findall(
                r'(?:window\.location|location\.href|location\.replace)\s*=',
                code, re.IGNORECASE,
            ))),
            "iframe_create_count": float(len(re.findall(
                r'createElement\s*\(\s*["\']iframe["\']', code, re.IGNORECASE
            ))),
            "event_listener_count": float(len(re.findall(
                r'\baddEventListener\s*\(', code
            ))),
            "try_catch_count": float(len(re.findall(
                r'\btry\s*\{', code
            ))),
            "hex_var_count": float(hex_vars),
        }

    @staticmethod
    def _names_entropy(names: list[str]) -> float:
        """변수/함수명의 평균 문자 엔트로피"""
        if not names:
            return 0.0

        entropies: list[float] = []
        for name in names:
            if len(name) < 2:
                continue
            counter = Counter(name.lower())
            length = len(name)
            entropy = -sum(
                (c / length) * math.log2(c / length)
                for c in counter.values()
                if c > 0
            )
            entropies.append(entropy)

        return sum(entropies) / max(len(entropies), 1)

    @staticmethod
    def _calc_obfuscation(
        var_entropy: float,
        hex_count: int,
        unicode_count: int,
        base64_count: int,
        code_density: float,
        avg_line: float,
        hex_vars: int,
    ) -> float:
        """
        난독화 점수 계산 (0.0~1.0)

        여러 지표를 가중 결합하여 난독화 수준을 추정합니다.
        """
        score = 0.0

        # 변수명 엔트로피 (높으면 난독화)
        if var_entropy > 2.5:
            score += min(1.0, var_entropy / 5.0) * 0.20

        # 인코딩 문자열
        encoding_total = hex_count + unicode_count + base64_count
        if encoding_total > 0:
            score += min(1.0, encoding_total / 10.0) * 0.25

        # 코드 밀도 (0.90 이상 = 미니파이/난독화)
        if code_density > 0.85:
            score += max(0.0, (code_density - 0.85) / 0.15) * 0.20

        # 평균 줄 길이 (200자 이상 = 난독화)
        if avg_line > 200:
            score += min(1.0, (avg_line - 200) / 500) * 0.15

        # _0x 변수 수
        if hex_vars > 0:
            score += min(1.0, hex_vars / 20.0) * 0.20

        return min(1.0, max(0.0, score))
