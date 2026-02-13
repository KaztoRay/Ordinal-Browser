"""
콘텐츠 보안 분석기 — CSP, 혼합 콘텐츠, SRI, Referrer Policy, CORS 설정 검사

웹 페이지의 보안 헤더와 리소스 로딩 방식을 분석하여
보안 취약점과 개선 권장 사항을 도출.

© 2026 KaztoRay — MIT License
"""

import re
import logging
from dataclasses import dataclass, field
from typing import Optional
from urllib.parse import urlparse

logger = logging.getLogger(__name__)


# ============================================================
# 데이터 클래스 정의
# ============================================================

@dataclass
class CSPReport:
    """Content-Security-Policy 분석 보고서"""
    raw_header: str                    # 원본 CSP 헤더
    directives: dict = field(default_factory=dict)  # 파싱된 디렉티브
    missing_directives: list = field(default_factory=list)  # 누락된 중요 디렉티브
    unsafe_inline: bool = False        # 'unsafe-inline' 사용 여부
    unsafe_eval: bool = False          # 'unsafe-eval' 사용 여부
    wildcard_sources: list = field(default_factory=list)  # 와일드카드(*) 사용 소스
    recommendations: list = field(default_factory=list)    # 개선 권장 사항
    score: int = 100                   # CSP 보안 점수 (0~100)


@dataclass
class MixedContentIssue:
    """혼합 콘텐츠(Mixed Content) 문제"""
    page_url: str           # HTTPS 페이지 URL
    resource_url: str       # HTTP 리소스 URL
    resource_type: str      # 리소스 유형 (script, image, stylesheet 등)
    severity: str           # 심각도 (high, medium, low)
    description: str        # 설명


@dataclass
class SRIIssue:
    """Subresource Integrity (SRI) 미사용 문제"""
    tag: str                # 태그 종류 (script, link)
    src: str                # 리소스 URL
    has_integrity: bool     # integrity 속성 존재 여부
    has_crossorigin: bool   # crossorigin 속성 존재 여부
    recommendation: str     # 권장 사항


@dataclass
class ReferrerPolicyReport:
    """Referrer-Policy 분석 보고서"""
    raw_header: str         # 원본 헤더 값
    policy: str             # 정규화된 정책 이름
    is_secure: bool         # 보안적으로 적절한지 여부
    risk_level: str         # 위험 수준 (safe, moderate, risky)
    recommendation: str     # 권장 사항


@dataclass
class CORSIssue:
    """CORS 설정 문제"""
    header: str             # 문제된 헤더
    value: str              # 헤더 값
    issue_type: str         # 문제 유형 (wildcard, null_origin, credentials 등)
    severity: str           # 심각도
    description: str        # 설명
    recommendation: str     # 권장 사항


# ============================================================
# 콘텐츠 보안 분석기
# ============================================================

class ContentSecurityAnalyzer:
    """
    웹 페이지의 콘텐츠 보안 정책 및 헤더 분석기.
    
    CSP, 혼합 콘텐츠, SRI, Referrer Policy, CORS 설정을 종합 분석.
    
    사용법:
        analyzer = ContentSecurityAnalyzer()
        csp = analyzer.analyze_csp("default-src 'self'; script-src 'unsafe-inline'")
        print(f"CSP 점수: {csp.score}/100")
    """

    # CSP에서 반드시 있어야 하는 중요 디렉티브
    CRITICAL_DIRECTIVES = [
        "default-src",
        "script-src",
        "style-src",
        "img-src",
        "connect-src",
        "font-src",
        "object-src",
        "frame-src",
        "base-uri",
        "form-action",
    ]

    # 안전한 Referrer Policy 목록
    SECURE_REFERRER_POLICIES = {
        "no-referrer",
        "same-origin",
        "strict-origin",
        "strict-origin-when-cross-origin",
    }

    # 위험한 Referrer Policy 목록
    RISKY_REFERRER_POLICIES = {
        "unsafe-url",
        "no-referrer-when-downgrade",
    }

    # 리소스 유형별 혼합 콘텐츠 심각도
    MIXED_CONTENT_SEVERITY = {
        "script": "high",
        "iframe": "high",
        "object": "high",
        "embed": "high",
        "stylesheet": "high",
        "font": "medium",
        "image": "low",
        "audio": "low",
        "video": "low",
        "track": "low",
    }

    def __init__(self):
        logger.info("[ContentSecurity] 분석기 초기화")

    # ============================================================
    # CSP 분석
    # ============================================================

    def analyze_csp(self, csp_header: str) -> CSPReport:
        """
        Content-Security-Policy 헤더를 파싱하고 보안 취약점 분석.
        
        점수 계산:
          - 기본 100점
          - 중요 디렉티브 누락: 각 -8점
          - unsafe-inline 사용: -15점
          - unsafe-eval 사용: -20점
          - 와일드카드(*) 소스: 각 -10점
          - default-src 미설정: -15점
        
        Args:
            csp_header: CSP 헤더 문자열
            
        Returns:
            CSPReport: 분석 보고서
        """
        if not csp_header or not csp_header.strip():
            return CSPReport(
                raw_header="",
                missing_directives=self.CRITICAL_DIRECTIVES.copy(),
                recommendations=["Content-Security-Policy 헤더를 설정하세요."],
                score=0,
            )

        logger.debug("[CSP] 분석 시작: %s", csp_header[:100])

        # 디렉티브 파싱
        directives = {}
        for directive in csp_header.split(";"):
            directive = directive.strip()
            if not directive:
                continue
            parts = directive.split()
            if parts:
                name = parts[0].lower()
                values = parts[1:] if len(parts) > 1 else []
                directives[name] = values

        # 누락된 중요 디렉티브 확인
        missing = []
        for critical in self.CRITICAL_DIRECTIVES:
            if critical not in directives:
                # default-src가 있으면 일부 디렉티브는 상속됨
                if "default-src" in directives and critical != "default-src":
                    # base-uri, form-action은 default-src에서 상속 안됨
                    if critical in ("base-uri", "form-action"):
                        missing.append(critical)
                else:
                    missing.append(critical)

        # unsafe-inline / unsafe-eval 감지
        has_unsafe_inline = False
        has_unsafe_eval = False
        wildcard_sources = []

        for name, values in directives.items():
            for val in values:
                val_lower = val.lower().strip("'\"")
                if val_lower == "unsafe-inline":
                    has_unsafe_inline = True
                if val_lower == "unsafe-eval":
                    has_unsafe_eval = True
                if val == "*":
                    wildcard_sources.append(name)

        # 점수 계산
        score = 100
        recommendations = []

        # 누락된 디렉티브 감점
        score -= len(missing) * 8
        if missing:
            recommendations.append(
                f"누락된 디렉티브를 추가하세요: {', '.join(missing)}"
            )

        # unsafe-inline 감점
        if has_unsafe_inline:
            score -= 15
            recommendations.append(
                "'unsafe-inline' 대신 nonce 또는 hash 기반 CSP를 사용하세요."
            )

        # unsafe-eval 감점
        if has_unsafe_eval:
            score -= 20
            recommendations.append(
                "'unsafe-eval'은 XSS 취약점을 유발합니다. 가능하면 제거하세요."
            )

        # 와일드카드 감점
        if wildcard_sources:
            score -= len(wildcard_sources) * 10
            recommendations.append(
                f"와일드카드(*) 소스를 구체적인 도메인으로 교체하세요: "
                f"{', '.join(wildcard_sources)}"
            )

        # default-src 미설정 추가 감점
        if "default-src" not in directives:
            score -= 15
            recommendations.append("default-src 디렉티브를 반드시 설정하세요.")

        # object-src 'none' 권장
        if "object-src" in directives:
            obj_values = directives["object-src"]
            if "'none'" not in [v.lower() for v in obj_values]:
                recommendations.append(
                    "object-src를 'none'으로 설정하여 플러그인 실행을 차단하세요."
                )

        score = max(0, min(100, score))

        report = CSPReport(
            raw_header=csp_header,
            directives=directives,
            missing_directives=missing,
            unsafe_inline=has_unsafe_inline,
            unsafe_eval=has_unsafe_eval,
            wildcard_sources=wildcard_sources,
            recommendations=recommendations,
            score=score,
        )

        logger.info("[CSP] 분석 완료 — 점수: %d/100, 권장사항: %d건",
                    score, len(recommendations))
        return report

    # ============================================================
    # 혼합 콘텐츠 검사
    # ============================================================

    def check_mixed_content(
        self, page_url: str, resource_urls: list[str]
    ) -> list[MixedContentIssue]:
        """
        HTTPS 페이지에서 HTTP 리소스를 로드하는 혼합 콘텐츠 문제 감지.
        
        Args:
            page_url: 페이지 URL (HTTPS여야 함)
            resource_urls: 페이지가 로드하는 리소스 URL 목록
            
        Returns:
            list[MixedContentIssue]: 발견된 혼합 콘텐츠 문제 목록
        """
        parsed_page = urlparse(page_url)

        # HTTP 페이지면 혼합 콘텐츠 아님
        if parsed_page.scheme != "https":
            logger.debug("[MixedContent] HTTP 페이지 — 검사 불필요")
            return []

        issues = []

        for resource_url in resource_urls:
            parsed_resource = urlparse(resource_url)

            # HTTP 리소스인 경우만 문제
            if parsed_resource.scheme != "http":
                continue

            # 리소스 유형 추정 (확장자 기반)
            path = parsed_resource.path.lower()
            resource_type = self._guess_resource_type(path)
            severity = self.MIXED_CONTENT_SEVERITY.get(resource_type, "medium")

            issues.append(MixedContentIssue(
                page_url=page_url,
                resource_url=resource_url,
                resource_type=resource_type,
                severity=severity,
                description=(
                    f"HTTPS 페이지에서 HTTP {resource_type} 리소스를 로드합니다: "
                    f"{resource_url[:80]}"
                ),
            ))

        logger.info("[MixedContent] %d건의 혼합 콘텐츠 발견 (%s)",
                    len(issues), page_url[:60])
        return issues

    def _guess_resource_type(self, path: str) -> str:
        """URL 경로에서 리소스 유형 추정"""
        ext_map = {
            ".js": "script",
            ".mjs": "script",
            ".css": "stylesheet",
            ".jpg": "image", ".jpeg": "image", ".png": "image",
            ".gif": "image", ".svg": "image", ".webp": "image",
            ".ico": "image", ".bmp": "image",
            ".woff": "font", ".woff2": "font", ".ttf": "font",
            ".eot": "font", ".otf": "font",
            ".mp3": "audio", ".ogg": "audio", ".wav": "audio",
            ".mp4": "video", ".webm": "video", ".avi": "video",
            ".swf": "object", ".pdf": "object",
        }

        for ext, rtype in ext_map.items():
            if path.endswith(ext):
                return rtype

        return "unknown"

    # ============================================================
    # SRI (Subresource Integrity) 검사
    # ============================================================

    def verify_sri(self, script_tags: list[dict]) -> list[SRIIssue]:
        """
        외부 스크립트/스타일시트의 SRI (integrity) 속성 검사.
        
        CDN 등 외부 소스에서 로드하는 리소스에 integrity 속성이 없으면
        공급망 공격에 취약.
        
        Args:
            script_tags: 태그 정보 딕셔너리 목록
                         [{"tag": "script", "src": "...", 
                           "integrity": "sha384-...", "crossorigin": "anonymous"}]
            
        Returns:
            list[SRIIssue]: SRI 관련 문제 목록
        """
        issues = []

        for tag_info in script_tags:
            tag = tag_info.get("tag", "script")
            src = tag_info.get("src", "")

            if not src:
                continue  # 인라인 스크립트는 SRI 불필요

            # 같은 오리진 리소스는 SRI 불필요
            parsed = urlparse(src)
            if not parsed.netloc:
                continue  # 상대 경로

            has_integrity = bool(tag_info.get("integrity", ""))
            has_crossorigin = bool(tag_info.get("crossorigin", ""))

            recommendation_parts = []

            if not has_integrity:
                recommendation_parts.append(
                    f"integrity 속성을 추가하세요 (sha384 권장)"
                )

            if has_integrity and not has_crossorigin:
                recommendation_parts.append(
                    "crossorigin='anonymous' 속성도 함께 추가하세요"
                )

            if recommendation_parts:
                issues.append(SRIIssue(
                    tag=tag,
                    src=src,
                    has_integrity=has_integrity,
                    has_crossorigin=has_crossorigin,
                    recommendation="; ".join(recommendation_parts),
                ))

        logger.info("[SRI] %d건의 SRI 문제 발견", len(issues))
        return issues

    # ============================================================
    # Referrer-Policy 분석
    # ============================================================

    def analyze_referrer_policy(self, header: str) -> ReferrerPolicyReport:
        """
        Referrer-Policy 헤더 분석.
        
        정보 누출을 방지하기 위한 적절한 Referrer Policy 설정 여부 확인.
        
        Args:
            header: Referrer-Policy 헤더 값
            
        Returns:
            ReferrerPolicyReport: 분석 보고서
        """
        if not header or not header.strip():
            return ReferrerPolicyReport(
                raw_header="",
                policy="(미설정)",
                is_secure=False,
                risk_level="risky",
                recommendation=(
                    "Referrer-Policy 헤더를 설정하세요. "
                    "권장: 'strict-origin-when-cross-origin'"
                ),
            )

        # 여러 정책이 쉼표로 구분될 수 있음 — 마지막 유효 정책 사용
        policies = [p.strip().lower() for p in header.split(",")]
        policy = policies[-1] if policies else ""

        is_secure = policy in self.SECURE_REFERRER_POLICIES
        is_risky = policy in self.RISKY_REFERRER_POLICIES

        if is_secure:
            risk_level = "safe"
            recommendation = "현재 설정이 적절합니다."
        elif is_risky:
            risk_level = "risky"
            recommendation = (
                f"'{policy}'는 크로스오리진 요청에서 전체 URL을 노출합니다. "
                "'strict-origin-when-cross-origin'으로 변경을 권장합니다."
            )
        else:
            risk_level = "moderate"
            recommendation = (
                f"'{policy}' 정책은 보통 수준입니다. "
                "보안 강화를 위해 'strict-origin-when-cross-origin'을 권장합니다."
            )

        report = ReferrerPolicyReport(
            raw_header=header,
            policy=policy,
            is_secure=is_secure,
            risk_level=risk_level,
            recommendation=recommendation,
        )

        logger.info("[ReferrerPolicy] 분석 완료 — 정책: %s, 위험도: %s",
                    policy, risk_level)
        return report

    # ============================================================
    # CORS 설정 오류 감지
    # ============================================================

    def detect_cors_misconfig(self, headers: dict) -> list[CORSIssue]:
        """
        CORS (Cross-Origin Resource Sharing) 헤더의 잘못된 설정 감지.
        
        확인 항목:
          - Access-Control-Allow-Origin: * (와일드카드)
          - Access-Control-Allow-Origin: null
          - Access-Control-Allow-Credentials: true + 와일드카드
          - Access-Control-Allow-Methods에 위험한 메서드
          - Access-Control-Max-Age 과도한 값
        
        Args:
            headers: HTTP 응답 헤더 딕셔너리 (키: 소문자)
            
        Returns:
            list[CORSIssue]: CORS 설정 문제 목록
        """
        issues = []

        # 헤더 이름 정규화 (소문자)
        normalized = {k.lower(): v for k, v in headers.items()}

        acao = normalized.get("access-control-allow-origin", "")
        acac = normalized.get("access-control-allow-credentials", "")
        acam = normalized.get("access-control-allow-methods", "")
        acah = normalized.get("access-control-allow-headers", "")
        acma = normalized.get("access-control-max-age", "")

        # 1. 와일드카드 Origin 허용
        if acao == "*":
            issues.append(CORSIssue(
                header="Access-Control-Allow-Origin",
                value="*",
                issue_type="wildcard",
                severity="medium",
                description=(
                    "모든 오리진에서의 접근을 허용합니다. "
                    "민감한 데이터가 포함된 API에서는 위험합니다."
                ),
                recommendation=(
                    "특정 도메인만 허용하도록 설정하세요. "
                    "예: Access-Control-Allow-Origin: https://example.com"
                ),
            ))

        # 2. null Origin 허용 (sandboxed iframe 공격)
        if acao.lower() == "null":
            issues.append(CORSIssue(
                header="Access-Control-Allow-Origin",
                value="null",
                issue_type="null_origin",
                severity="high",
                description=(
                    "null 오리진을 허용합니다. 샌드박스된 iframe이나 "
                    "data: URL에서 악용될 수 있습니다."
                ),
                recommendation="null 오리진을 허용하지 마세요.",
            ))

        # 3. Credentials + 와일드카드 (실제로 브라우저가 차단하지만 의도가 잘못됨)
        if acac.lower() == "true" and acao == "*":
            issues.append(CORSIssue(
                header="Access-Control-Allow-Credentials",
                value="true",
                issue_type="credentials_wildcard",
                severity="high",
                description=(
                    "credentials: true와 와일드카드 Origin을 동시에 사용합니다. "
                    "브라우저가 차단하지만, 서버 설정 의도가 잘못되었습니다."
                ),
                recommendation=(
                    "credentials 사용 시 특정 Origin을 명시하세요."
                ),
            ))

        # 4. Credentials + 동적 Origin 반사 (잠재적 취약점)
        if acac.lower() == "true" and acao and acao != "*":
            # Origin 헤더를 그대로 반사하는 경우 (요청마다 다른 값)
            # 여기서는 패턴 감지만 경고
            issues.append(CORSIssue(
                header="Access-Control-Allow-Credentials",
                value="true",
                issue_type="credentials_reflection",
                severity="medium",
                description=(
                    "credentials와 특정 Origin을 사용합니다. "
                    "요청의 Origin을 그대로 반사하는 경우 CSRF에 취약할 수 있습니다."
                ),
                recommendation=(
                    "허용할 Origin을 화이트리스트로 검증하세요."
                ),
            ))

        # 5. 위험한 HTTP 메서드 허용
        if acam:
            dangerous_methods = {"DELETE", "PUT", "PATCH"}
            allowed_methods = {m.strip().upper() for m in acam.split(",")}
            exposed_dangerous = allowed_methods & dangerous_methods

            if exposed_dangerous:
                issues.append(CORSIssue(
                    header="Access-Control-Allow-Methods",
                    value=acam,
                    issue_type="dangerous_methods",
                    severity="medium",
                    description=(
                        f"위험한 HTTP 메서드가 CORS에 허용되어 있습니다: "
                        f"{', '.join(exposed_dangerous)}"
                    ),
                    recommendation=(
                        "필요한 메서드만 명시적으로 허용하세요."
                    ),
                ))

        # 6. 과도한 Max-Age (캐시 문제)
        if acma:
            try:
                max_age = int(acma)
                if max_age > 86400:  # 24시간 이상
                    issues.append(CORSIssue(
                        header="Access-Control-Max-Age",
                        value=acma,
                        issue_type="excessive_max_age",
                        severity="low",
                        description=(
                            f"CORS preflight 캐시가 {max_age}초 ({max_age // 3600}시간)로 "
                            "과도하게 설정되어 있습니다."
                        ),
                        recommendation=(
                            "Max-Age를 86400초(24시간) 이하로 설정하세요."
                        ),
                    ))
            except ValueError:
                pass

        logger.info("[CORS] %d건의 CORS 설정 문제 발견", len(issues))
        return issues
