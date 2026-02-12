"""
보안 에이전트 코어 모듈
======================

비동기 위협 분석 파이프라인을 관리하는 핵심 SecurityAgent 클래스.
모든 분석기를 조율하고, LLM 프롬프트 템플릿을 통해
URL/스크립트/페이지 분석을 수행합니다.

ThreatReport를 생성하며, 결과를 캐싱합니다.
"""

from __future__ import annotations

import asyncio
import hashlib
import logging
import time
from dataclasses import dataclass, field
from enum import Enum, IntEnum
from typing import Any, Optional

from agent.core.config import AgentConfig

logger = logging.getLogger(__name__)


# ============================================================
# 열거형 및 데이터 클래스
# ============================================================

class ThreatLevel(IntEnum):
    """위협 수준 (proto와 동기화)"""
    SAFE = 0
    LOW = 1
    MEDIUM = 2
    HIGH = 3
    CRITICAL = 4


class ThreatType(str, Enum):
    """위협 유형"""
    PHISHING = "phishing"
    MALWARE = "malware"
    XSS = "xss"
    PRIVACY = "privacy"
    CERT = "cert"


@dataclass
class ThreatDetail:
    """개별 위협 상세 정보"""
    threat_type: ThreatType
    threat_level: ThreatLevel
    confidence: float  # 0.0 ~ 1.0
    description: str
    indicators: list[str] = field(default_factory=list)
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass
class ThreatReport:
    """통합 위협 보고서"""
    url: str
    overall_level: ThreatLevel = ThreatLevel.SAFE
    overall_score: float = 0.0  # 0.0 (안전) ~ 1.0 (위험)
    details: list[ThreatDetail] = field(default_factory=list)
    recommendations: list[str] = field(default_factory=list)
    analysis_time_ms: float = 0.0
    cached: bool = False
    timestamp: float = field(default_factory=time.time)

    def add_detail(self, detail: ThreatDetail) -> None:
        """위협 상세 정보 추가 및 전체 점수 재계산"""
        self.details.append(detail)
        self._recalculate()

    def _recalculate(self) -> None:
        """전체 위협 수준과 점수를 재계산"""
        if not self.details:
            self.overall_level = ThreatLevel.SAFE
            self.overall_score = 0.0
            return
        # 가장 높은 위협 수준 선택
        self.overall_level = max(d.threat_level for d in self.details)
        # 가중 평균 점수 계산 (높은 신뢰도에 더 큰 가중치)
        total_weight = sum(d.confidence for d in self.details)
        if total_weight > 0:
            weighted_sum = sum(
                d.confidence * (d.threat_level / ThreatLevel.CRITICAL)
                for d in self.details
            )
            self.overall_score = min(1.0, weighted_sum / total_weight)
        else:
            self.overall_score = 0.0


# ============================================================
# LLM 프롬프트 템플릿
# ============================================================

class PromptTemplates:
    """LLM 분석용 시스템/유저 프롬프트 템플릿"""

    # ---- URL 분석 프롬프트 ----
    URL_SYSTEM_PROMPT: str = (
        "당신은 웹 보안 전문가입니다. URL을 분석하여 피싱, 악성코드, "
        "의심스러운 패턴을 탐지합니다. 반드시 JSON 형식으로 응답하세요.\n"
        "응답 형식:\n"
        "{\n"
        '  "threat_level": "SAFE|LOW|MEDIUM|HIGH|CRITICAL",\n'
        '  "threat_types": ["phishing", "malware", ...],\n'
        '  "confidence": 0.0~1.0,\n'
        '  "reasoning": "분석 근거",\n'
        '  "indicators": ["지표1", "지표2"],\n'
        '  "recommendation": "권장 조치"\n'
        "}"
    )

    URL_USER_PROMPT: str = (
        "다음 URL의 보안 위협을 분석하세요:\n\n"
        "URL: {url}\n"
        "도메인: {domain}\n"
        "서브도메인 수: {subdomain_count}\n"
        "URL 길이: {url_length}\n"
        "특수 문자 비율: {special_char_ratio:.2f}\n"
        "IP 주소 사용 여부: {uses_ip}\n"
        "HTTPS 여부: {is_https}\n"
        "URL 엔트로피: {entropy:.2f}\n\n"
        "이 URL이 피싱이나 악성 사이트일 가능성을 평가하세요."
    )

    # ---- 스크립트 분석 프롬프트 ----
    SCRIPT_SYSTEM_PROMPT: str = (
        "당신은 JavaScript 보안 분석 전문가입니다. 제공된 코드에서 "
        "악성 패턴, 난독화, 데이터 유출, 크립토마이닝 등을 탐지합니다.\n"
        "반드시 JSON 형식으로 응답하세요.\n"
        "응답 형식:\n"
        "{\n"
        '  "threat_level": "SAFE|LOW|MEDIUM|HIGH|CRITICAL",\n'
        '  "malware_type": "none|obfuscation|data_exfil|crypto_miner|exploit",\n'
        '  "confidence": 0.0~1.0,\n'
        '  "reasoning": "chain-of-thought 분석",\n'
        '  "suspicious_patterns": ["패턴1", "패턴2"],\n'
        '  "behavior_prediction": "예측되는 동작"\n'
        "}"
    )

    SCRIPT_USER_PROMPT: str = (
        "다음 JavaScript 코드의 보안 위협을 분석하세요:\n\n"
        "```javascript\n{code}\n```\n\n"
        "코드 통계:\n"
        "- eval() 사용 횟수: {eval_count}\n"
        "- document.write() 사용 횟수: {doc_write_count}\n"
        "- 인코딩된 문자열 수: {encoded_string_count}\n"
        "- 변수명 엔트로피: {var_entropy:.2f}\n"
        "- 난독화 점수: {obfuscation_score:.2f}\n\n"
        "이 코드가 악성인지 분석하세요. chain-of-thought로 추론하세요."
    )

    # ---- 페이지 분석 프롬프트 ----
    PAGE_SYSTEM_PROMPT: str = (
        "당신은 웹 페이지 보안 분석 전문가입니다. "
        "HTML 콘텐츠와 URL을 종합적으로 분석하여 "
        "피싱, XSS, 악성코드, 프라이버시 침해를 탐지합니다.\n"
        "반드시 JSON 형식으로 응답하세요.\n"
        "응답 형식:\n"
        "{\n"
        '  "overall_threat_level": "SAFE|LOW|MEDIUM|HIGH|CRITICAL",\n'
        '  "threats": [\n'
        "    {\n"
        '      "type": "phishing|malware|xss|privacy",\n'
        '      "level": "SAFE|LOW|MEDIUM|HIGH|CRITICAL",\n'
        '      "confidence": 0.0~1.0,\n'
        '      "description": "설명"\n'
        "    }\n"
        "  ],\n"
        '  "security_score": 0~100,\n'
        '  "recommendations": ["권장1", "권장2"]\n'
        "}"
    )

    PAGE_USER_PROMPT: str = (
        "다음 웹 페이지의 보안을 종합 분석하세요:\n\n"
        "URL: {url}\n"
        "페이지 제목: {title}\n\n"
        "HTML 요약:\n"
        "- 폼 수: {form_count}\n"
        "- 비밀번호 필드 수: {password_field_count}\n"
        "- 외부 스크립트 수: {external_script_count}\n"
        "- 외부 리소스 비율: {external_resource_ratio:.2f}\n"
        "- iframe 수: {iframe_count}\n"
        "- 숨겨진 요소 수: {hidden_element_count}\n\n"
        "HTML 발췌 (첫 2000자):\n"
        "```html\n{html_snippet}\n```\n\n"
        "종합적인 보안 위협 분석을 수행하세요."
    )


# ============================================================
# 캐시 항목
# ============================================================

@dataclass
class _CacheEntry:
    """분석 결과 캐시 항목"""
    report: ThreatReport
    created_at: float
    ttl: float

    @property
    def is_expired(self) -> bool:
        return (time.time() - self.created_at) > self.ttl


# ============================================================
# SecurityAgent 메인 클래스
# ============================================================

class SecurityAgent:
    """
    비동기 보안 분석 에이전트

    모든 분석기(PhishingAnalyzer, MalwareAnalyzer, PrivacyAnalyzer)를
    조율하여 URL, 스크립트, 페이지에 대한 통합 위협 보고서를 생성합니다.

    LLM 추론, 결과 캐싱, 병렬 분석을 지원합니다.
    """

    def __init__(self, config: Optional[AgentConfig] = None) -> None:
        """
        보안 에이전트 초기화

        Args:
            config: 에이전트 설정. None이면 기본 설정 사용.
        """
        self.config = config or AgentConfig()
        self._cache: dict[str, _CacheEntry] = {}
        self._cache_lock = asyncio.Lock()
        self._initialized = False

        # 분석기는 지연 초기화 (import 순환 방지)
        self._phishing_analyzer: Any = None
        self._malware_analyzer: Any = None
        self._privacy_analyzer: Any = None
        self._llm_inference: Any = None

        logger.info("SecurityAgent 인스턴스 생성 (v%s)", self.config.version)

    async def initialize(self) -> None:
        """
        에이전트 및 모든 분석기 초기화

        분석기 인스턴스를 생성하고, LLM 추론 엔진을 준비합니다.
        """
        if self._initialized:
            logger.warning("SecurityAgent가 이미 초기화되어 있습니다")
            return

        logger.info("SecurityAgent 초기화 시작...")

        # 분석기 지연 임포트 및 초기화
        from agent.analyzers.phishing_analyzer import PhishingAnalyzer
        from agent.analyzers.malware_analyzer import MalwareAnalyzer
        from agent.analyzers.privacy_analyzer import PrivacyAnalyzer

        self._phishing_analyzer = PhishingAnalyzer(self.config)
        self._malware_analyzer = MalwareAnalyzer(self.config)
        self._privacy_analyzer = PrivacyAnalyzer(self.config)

        # LLM 추론 엔진 초기화 (선택적)
        try:
            from agent.models.inference import LLMInference
            self._llm_inference = LLMInference(self.config.llm)
            logger.info("LLM 추론 엔진 초기화 완료")
        except ImportError:
            logger.warning("LLM 추론 모듈을 찾을 수 없습니다. LLM 분석 비활성화.")
        except Exception as e:
            logger.warning("LLM 추론 엔진 초기화 실패: %s", e)

        self._initialized = True
        logger.info("SecurityAgent 초기화 완료")

    async def shutdown(self) -> None:
        """에이전트 종료 및 리소스 해제"""
        logger.info("SecurityAgent 종료 중...")
        self._cache.clear()
        self._initialized = False
        logger.info("SecurityAgent 종료 완료")

    # ============================
    # URL 분석
    # ============================

    async def analyze_url(self, url: str, use_llm: bool = True) -> ThreatReport:
        """
        URL 보안 분석

        피싱 분석기를 사용하여 URL의 위협 수준을 평가합니다.
        캐시된 결과가 있으면 즉시 반환합니다.

        Args:
            url: 분석할 URL
            use_llm: LLM 심층 분석 사용 여부

        Returns:
            ThreatReport: 위협 보고서
        """
        self._ensure_initialized()

        # 캐시 확인
        cache_key = self._make_cache_key("url", url)
        cached = await self._get_cached(cache_key)
        if cached is not None:
            return cached

        start_time = time.monotonic()
        report = ThreatReport(url=url)

        # 피싱 분석기 실행
        phishing_detail = await self._phishing_analyzer.analyze_url(url)
        if phishing_detail is not None:
            report.add_detail(phishing_detail)

        # LLM 심층 분석 (선택적)
        if use_llm and self._llm_inference is not None:
            llm_detail = await self._analyze_url_with_llm(url)
            if llm_detail is not None:
                report.add_detail(llm_detail)

        # 권장 사항 생성
        report.recommendations = self._generate_recommendations(report)
        report.analysis_time_ms = (time.monotonic() - start_time) * 1000

        # 캐시 저장
        await self._set_cached(cache_key, report)

        logger.info(
            "URL 분석 완료: %s → %s (%.1fms)",
            url, report.overall_level.name, report.analysis_time_ms
        )
        return report

    # ============================
    # 스크립트 분석
    # ============================

    async def analyze_script(self, code: str, source_url: str = "") -> ThreatReport:
        """
        JavaScript 코드 보안 분석

        악성 패턴, 난독화, 데이터 유출 가능성을 검사합니다.

        Args:
            code: JavaScript 소스 코드
            source_url: 스크립트 출처 URL

        Returns:
            ThreatReport: 위협 보고서
        """
        self._ensure_initialized()

        # 캐시 확인 (코드 해시 기반)
        code_hash = hashlib.sha256(code.encode()).hexdigest()[:16]
        cache_key = self._make_cache_key("script", code_hash)
        cached = await self._get_cached(cache_key)
        if cached is not None:
            return cached

        start_time = time.monotonic()
        report = ThreatReport(url=source_url or f"script:{code_hash}")

        # 악성코드 분석기 실행
        malware_detail = await self._malware_analyzer.analyze_script(code)
        if malware_detail is not None:
            report.add_detail(malware_detail)

        # LLM 심층 분석 (선택적)
        if self._llm_inference is not None and len(code) <= 10000:
            llm_detail = await self._analyze_script_with_llm(code)
            if llm_detail is not None:
                report.add_detail(llm_detail)

        report.recommendations = self._generate_recommendations(report)
        report.analysis_time_ms = (time.monotonic() - start_time) * 1000

        await self._set_cached(cache_key, report)

        logger.info(
            "스크립트 분석 완료: %s → %s (%.1fms)",
            report.url, report.overall_level.name, report.analysis_time_ms
        )
        return report

    # ============================
    # 페이지 종합 분석
    # ============================

    async def analyze_page(
        self,
        url: str,
        html_content: str,
        use_llm: bool = True,
    ) -> ThreatReport:
        """
        웹 페이지 종합 보안 분석

        URL, HTML 콘텐츠, 스크립트를 동시에 분석하여
        통합 위협 보고서를 생성합니다.

        Args:
            url: 페이지 URL
            html_content: HTML 소스 코드
            use_llm: LLM 심층 분석 사용 여부

        Returns:
            ThreatReport: 통합 위협 보고서
        """
        self._ensure_initialized()

        # 캐시 확인
        content_hash = hashlib.sha256(
            f"{url}:{html_content[:1000]}".encode()
        ).hexdigest()[:16]
        cache_key = self._make_cache_key("page", content_hash)
        cached = await self._get_cached(cache_key)
        if cached is not None:
            return cached

        start_time = time.monotonic()
        report = ThreatReport(url=url)

        # 모든 분석기를 병렬로 실행
        tasks = [
            self._phishing_analyzer.analyze_url(url),
            self._phishing_analyzer.analyze_content(url, html_content),
            self._malware_analyzer.analyze_html(html_content),
            self._privacy_analyzer.analyze_page(url, html_content),
        ]

        results = await asyncio.gather(*tasks, return_exceptions=True)

        # 결과 수집
        for result in results:
            if isinstance(result, Exception):
                logger.error("분석기 오류: %s", result)
                continue
            if result is not None:
                if isinstance(result, list):
                    for detail in result:
                        report.add_detail(detail)
                else:
                    report.add_detail(result)

        # LLM 페이지 종합 분석 (선택적)
        if use_llm and self._llm_inference is not None:
            llm_detail = await self._analyze_page_with_llm(url, html_content)
            if llm_detail is not None:
                report.add_detail(llm_detail)

        report.recommendations = self._generate_recommendations(report)
        report.analysis_time_ms = (time.monotonic() - start_time) * 1000

        await self._set_cached(cache_key, report)

        logger.info(
            "페이지 분석 완료: %s → %s (점수: %.2f, %.1fms)",
            url, report.overall_level.name,
            report.overall_score, report.analysis_time_ms,
        )
        return report

    # ============================
    # LLM 분석 내부 메서드
    # ============================

    async def _analyze_url_with_llm(self, url: str) -> Optional[ThreatDetail]:
        """LLM을 사용한 URL 심층 분석"""
        try:
            from agent.utils.feature_extractor import URLFeatureExtractor
            extractor = URLFeatureExtractor()
            features = extractor.extract(url)

            prompt = PromptTemplates.URL_USER_PROMPT.format(
                url=url,
                domain=features.get("domain", ""),
                subdomain_count=features.get("subdomain_count", 0),
                url_length=features.get("length", 0),
                special_char_ratio=features.get("special_char_ratio", 0.0),
                uses_ip=features.get("is_ip_address", False),
                is_https=features.get("is_https", False),
                entropy=features.get("entropy", 0.0),
            )

            response = await self._llm_inference.generate(
                system_prompt=PromptTemplates.URL_SYSTEM_PROMPT,
                user_prompt=prompt,
            )

            return self._parse_llm_threat_response(response, ThreatType.PHISHING)
        except Exception as e:
            logger.error("LLM URL 분석 실패: %s", e)
            return None

    async def _analyze_script_with_llm(self, code: str) -> Optional[ThreatDetail]:
        """LLM을 사용한 스크립트 심층 분석"""
        try:
            from agent.utils.feature_extractor import JSFeatureExtractor
            extractor = JSFeatureExtractor()
            features = extractor.extract(code)

            # 코드가 너무 길면 앞부분만 전송
            code_snippet = code[:4000] if len(code) > 4000 else code

            prompt = PromptTemplates.SCRIPT_USER_PROMPT.format(
                code=code_snippet,
                eval_count=features.get("eval_count", 0),
                doc_write_count=features.get("document_write_count", 0),
                encoded_string_count=features.get("encoded_string_count", 0),
                var_entropy=features.get("variable_name_entropy", 0.0),
                obfuscation_score=features.get("obfuscation_score", 0.0),
            )

            response = await self._llm_inference.generate(
                system_prompt=PromptTemplates.SCRIPT_SYSTEM_PROMPT,
                user_prompt=prompt,
            )

            return self._parse_llm_threat_response(response, ThreatType.MALWARE)
        except Exception as e:
            logger.error("LLM 스크립트 분석 실패: %s", e)
            return None

    async def _analyze_page_with_llm(
        self, url: str, html_content: str
    ) -> Optional[ThreatDetail]:
        """LLM을 사용한 페이지 종합 심층 분석"""
        try:
            from agent.utils.feature_extractor import DOMFeatureExtractor
            extractor = DOMFeatureExtractor()
            features = extractor.extract(html_content)

            html_snippet = html_content[:2000] if len(html_content) > 2000 else html_content

            prompt = PromptTemplates.PAGE_USER_PROMPT.format(
                url=url,
                title=features.get("title", ""),
                form_count=features.get("form_count", 0),
                password_field_count=features.get("password_field_count", 0),
                external_script_count=features.get("external_script_count", 0),
                external_resource_ratio=features.get("external_resource_ratio", 0.0),
                iframe_count=features.get("iframe_count", 0),
                hidden_element_count=features.get("hidden_element_count", 0),
                html_snippet=html_snippet,
            )

            response = await self._llm_inference.generate(
                system_prompt=PromptTemplates.PAGE_SYSTEM_PROMPT,
                user_prompt=prompt,
            )

            return self._parse_llm_threat_response(response, ThreatType.PHISHING)
        except Exception as e:
            logger.error("LLM 페이지 분석 실패: %s", e)
            return None

    # ============================
    # LLM 응답 파싱
    # ============================

    def _parse_llm_threat_response(
        self, response: dict[str, Any], default_type: ThreatType
    ) -> Optional[ThreatDetail]:
        """
        LLM JSON 응답을 ThreatDetail로 변환

        Args:
            response: LLM이 반환한 파싱된 JSON
            default_type: 기본 위협 유형

        Returns:
            ThreatDetail 또는 None (안전한 경우)
        """
        if not response:
            return None

        # 위협 수준 파싱
        level_str = response.get("threat_level", "SAFE").upper()
        level_map = {
            "SAFE": ThreatLevel.SAFE,
            "LOW": ThreatLevel.LOW,
            "MEDIUM": ThreatLevel.MEDIUM,
            "HIGH": ThreatLevel.HIGH,
            "CRITICAL": ThreatLevel.CRITICAL,
        }
        threat_level = level_map.get(level_str, ThreatLevel.SAFE)

        # SAFE이면 None 반환
        if threat_level == ThreatLevel.SAFE:
            return None

        confidence = float(response.get("confidence", 0.5))
        confidence = max(0.0, min(1.0, confidence))

        # 위협 유형 결정
        threat_types = response.get("threat_types", [])
        if threat_types:
            type_map = {
                "phishing": ThreatType.PHISHING,
                "malware": ThreatType.MALWARE,
                "xss": ThreatType.XSS,
                "privacy": ThreatType.PRIVACY,
                "cert": ThreatType.CERT,
            }
            actual_type = type_map.get(threat_types[0], default_type)
        else:
            actual_type = default_type

        return ThreatDetail(
            threat_type=actual_type,
            threat_level=threat_level,
            confidence=confidence,
            description=response.get("reasoning", "LLM 분석 결과"),
            indicators=response.get("indicators", response.get("suspicious_patterns", [])),
            metadata={
                "source": "llm",
                "recommendation": response.get("recommendation", ""),
                "behavior_prediction": response.get("behavior_prediction", ""),
            },
        )

    # ============================
    # 권장 사항 생성
    # ============================

    def _generate_recommendations(self, report: ThreatReport) -> list[str]:
        """위협 보고서 기반 권장 사항 생성"""
        recommendations: list[str] = []

        if report.overall_level == ThreatLevel.SAFE:
            recommendations.append("이 페이지는 안전한 것으로 판단됩니다.")
            return recommendations

        # 위협 유형별 권장 사항
        threat_types_found = {d.threat_type for d in report.details}

        if ThreatType.PHISHING in threat_types_found:
            recommendations.append(
                "⚠️ 피싱 의심: 이 사이트에 개인 정보를 입력하지 마세요."
            )
            recommendations.append(
                "URL을 주의 깊게 확인하고, 공식 사이트 주소와 비교하세요."
            )

        if ThreatType.MALWARE in threat_types_found:
            recommendations.append(
                "🚨 악성코드 의심: 이 페이지의 파일을 다운로드하지 마세요."
            )
            recommendations.append(
                "JavaScript 실행이 차단될 수 있습니다."
            )

        if ThreatType.XSS in threat_types_found:
            recommendations.append(
                "⚡ XSS 취약점 탐지: 이 페이지에서 입력한 데이터가 유출될 수 있습니다."
            )

        if ThreatType.PRIVACY in threat_types_found:
            recommendations.append(
                "👁️ 프라이버시 위협: 추적기가 탐지되었습니다. "
                "추적 차단 기능을 활성화하세요."
            )

        # 심각도별 추가 권장 사항
        if report.overall_level >= ThreatLevel.HIGH:
            recommendations.append(
                "🛑 높은 위협 수준: 이 사이트를 즉시 떠나는 것을 권장합니다."
            )
        elif report.overall_level >= ThreatLevel.MEDIUM:
            recommendations.append(
                "⚠️ 중간 위협 수준: 주의하여 이용하세요."
            )

        return recommendations

    # ============================
    # 캐시 관리
    # ============================

    def _make_cache_key(self, prefix: str, value: str) -> str:
        """캐시 키 생성"""
        return f"{prefix}:{hashlib.md5(value.encode()).hexdigest()}"

    async def _get_cached(self, key: str) -> Optional[ThreatReport]:
        """캐시에서 분석 결과 조회"""
        if not self.config.llm.cache_enabled:
            return None

        async with self._cache_lock:
            entry = self._cache.get(key)
            if entry is None:
                return None
            if entry.is_expired:
                del self._cache[key]
                return None
            # 캐시 히트 표시
            report = entry.report
            report.cached = True
            return report

    async def _set_cached(self, key: str, report: ThreatReport) -> None:
        """분석 결과를 캐시에 저장"""
        if not self.config.llm.cache_enabled:
            return

        async with self._cache_lock:
            # 캐시 크기 제한 확인
            if len(self._cache) >= self.config.llm.cache_max_size:
                # 가장 오래된 항목 제거 (간단한 FIFO)
                oldest_key = next(iter(self._cache))
                del self._cache[oldest_key]

            self._cache[key] = _CacheEntry(
                report=report,
                created_at=time.time(),
                ttl=float(self.config.llm.cache_ttl_seconds),
            )

    async def clear_cache(self) -> None:
        """캐시 전체 삭제"""
        async with self._cache_lock:
            self._cache.clear()
            logger.info("분석 결과 캐시 초기화 완료")

    @property
    def cache_size(self) -> int:
        """현재 캐시 항목 수"""
        return len(self._cache)

    # ============================
    # 유틸리티
    # ============================

    def _ensure_initialized(self) -> None:
        """초기화 여부 확인"""
        if not self._initialized:
            raise RuntimeError(
                "SecurityAgent가 초기화되지 않았습니다. "
                "await agent.initialize()를 먼저 호출하세요."
            )

    @property
    def is_initialized(self) -> bool:
        """초기화 상태"""
        return self._initialized

    def __repr__(self) -> str:
        return (
            f"SecurityAgent(name={self.config.agent_name!r}, "
            f"version={self.config.version!r}, "
            f"initialized={self._initialized})"
        )
