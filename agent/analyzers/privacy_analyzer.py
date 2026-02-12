"""
프라이버시 분석기
=================

웹 추적기, 브라우저 핑거프린팅, 써드파티 쿠키,
픽셀 추적기, 데이터 유출 위험을 분석합니다.

탐지 항목:
- 추적기 DB 매칭 (EasyList/EasyPrivacy URL 패턴)
- 핑거프린팅 (Canvas, WebGL, AudioContext, 폰트, Navigator)
- 써드파티 쿠키 분석
- 픽셀 추적기 탐지
- 데이터 유출 위험 점수
"""

from __future__ import annotations

import logging
import re
from dataclasses import dataclass, field
from typing import Optional
from urllib.parse import urlparse

from agent.core.config import AgentConfig
from agent.core.agent import ThreatDetail, ThreatLevel, ThreatType

logger = logging.getLogger(__name__)


# ============================================================
# 알려진 추적기 패턴 (EasyList/EasyPrivacy 기반)
# ============================================================

TRACKER_PATTERNS: list[str] = [
    # Google Analytics / Ads
    r"google-analytics\.com",
    r"googletagmanager\.com",
    r"googlesyndication\.com",
    r"doubleclick\.net",
    r"googleadservices\.com",
    r"google\.com/ads",
    r"pagead2\.googlesyndication\.com",
    r"adservice\.google\.",
    # Facebook
    r"facebook\.com/tr",
    r"connect\.facebook\.net",
    r"pixel\.facebook\.com",
    r"facebook\.net/signals",
    # Twitter/X
    r"analytics\.twitter\.com",
    r"t\.co/i/adsct",
    r"platform\.twitter\.com/widgets",
    # Microsoft / LinkedIn
    r"bat\.bing\.com",
    r"clarity\.ms",
    r"snap\.licdn\.com",
    r"linkedin\.com/px",
    # Amazon
    r"amazon-adsystem\.com",
    r"assoc-amazon\.com",
    # 범용 추적기
    r"scorecardresearch\.com",
    r"quantserve\.com",
    r"hotjar\.com",
    r"fullstory\.com",
    r"mouseflow\.com",
    r"crazyegg\.com",
    r"mixpanel\.com",
    r"segment\.com/analytics",
    r"amplitude\.com",
    r"newrelic\.com",
    r"nr-data\.net",
    r"sentry\.io",
    r"bugsnag\.com",
    r"optimizely\.com",
    r"cdn\.heapanalytics\.com",
    r"intercom\.io",
    r"crisp\.chat",
    r"tawk\.to",
    r"livechatinc\.com",
    r"zopim\.com",
    r"adnxs\.com",
    r"criteo\.com",
    r"taboola\.com",
    r"outbrain\.com",
    r"chartbeat\.com",
    r"comscore\.com",
    r"matomo\.",
    r"piwik\.",
    r"hubspot\.com/analytics",
    r"pardot\.com",
    r"marketo\.net",
    r"munchkin\.marketo\.net",
    r"ad\.doubleclick\.net",
    r"cm\.g\.doubleclick\.net",
    r"pubads\.g\.doubleclick\.net",
    r"stats\.wp\.com",
    r"pixel\.wp\.com",
    # 한국 추적기
    r"wcs\.naver\.net",
    r"ntm\.naver\.com",
    r"ssl\.pstatic\.net/tveta",
    r"analytics\.kakao\.com",
    r"t1\.daumcdn\.net/kas",
]

# 컴파일된 추적기 패턴
COMPILED_TRACKER_PATTERNS: list[re.Pattern] = [
    re.compile(p, re.IGNORECASE) for p in TRACKER_PATTERNS
]


# ============================================================
# 핑거프린팅 API 패턴
# ============================================================

@dataclass
class FingerprintPattern:
    """핑거프린팅 API 탐지 패턴"""
    name: str
    pattern: re.Pattern
    severity: float  # 심각도 가중치 (0.0~1.0)
    description: str


FINGERPRINT_PATTERNS: list[FingerprintPattern] = [
    # Canvas 핑거프린팅
    FingerprintPattern(
        name="canvas_fingerprint",
        pattern=re.compile(
            r'(?:\.toDataURL\s*\(|\.getImageData\s*\(|'
            r'getContext\s*\(\s*["\']2d["\'])',
            re.IGNORECASE,
        ),
        severity=0.7,
        description="Canvas API를 이용한 브라우저 핑거프린팅",
    ),
    # WebGL 핑거프린팅
    FingerprintPattern(
        name="webgl_fingerprint",
        pattern=re.compile(
            r'(?:getContext\s*\(\s*["\'](?:webgl|experimental-webgl)["\']|'
            r'\.getExtension\s*\(\s*["\']WEBGL_debug_renderer_info["\']|'
            r'UNMASKED_(?:VENDOR|RENDERER)_WEBGL)',
            re.IGNORECASE,
        ),
        severity=0.8,
        description="WebGL API를 이용한 GPU 핑거프린팅",
    ),
    # AudioContext 핑거프린팅
    FingerprintPattern(
        name="audio_fingerprint",
        pattern=re.compile(
            r'(?:AudioContext|webkitAudioContext|OfflineAudioContext)'
            r'[^;]*(?:createOscillator|createAnalyser|createDynamicsCompressor'
            r'|destination|getFloatFrequencyData)',
            re.IGNORECASE | re.DOTALL,
        ),
        severity=0.8,
        description="AudioContext API를 이용한 오디오 핑거프린팅",
    ),
    # 폰트 열거 핑거프린팅
    FingerprintPattern(
        name="font_enumeration",
        pattern=re.compile(
            r'(?:(?:document\.fonts|FontFace)\s*[^;]*(?:check|load)|'
            r'(?:offsetWidth|offsetHeight|getBoundingClientRect)[^;]*'
            r'(?:monospace|serif|sans-serif))',
            re.IGNORECASE | re.DOTALL,
        ),
        severity=0.6,
        description="CSS 폰트 열거를 통한 핑거프린팅",
    ),
    # Navigator 속성 수집
    FingerprintPattern(
        name="navigator_fingerprint",
        pattern=re.compile(
            r'navigator\s*\.\s*(?:plugins|mimeTypes|languages|'
            r'hardwareConcurrency|deviceMemory|maxTouchPoints|'
            r'connection|getBattery|mediaDevices\.enumerateDevices)',
            re.IGNORECASE,
        ),
        severity=0.5,
        description="Navigator 속성을 수집하는 핑거프린팅",
    ),
    # Screen 속성 수집
    FingerprintPattern(
        name="screen_fingerprint",
        pattern=re.compile(
            r'screen\s*\.\s*(?:width|height|availWidth|availHeight|'
            r'colorDepth|pixelDepth|orientation)',
            re.IGNORECASE,
        ),
        severity=0.3,
        description="Screen API를 이용한 디스플레이 핑거프린팅",
    ),
    # WebRTC IP 유출
    FingerprintPattern(
        name="webrtc_leak",
        pattern=re.compile(
            r'(?:RTCPeerConnection|webkitRTCPeerConnection|mozRTCPeerConnection)'
            r'[^;]*(?:createDataChannel|createOffer|onicecandidate)',
            re.IGNORECASE | re.DOTALL,
        ),
        severity=0.9,
        description="WebRTC를 이용한 로컬 IP 주소 유출",
    ),
    # 배터리 상태
    FingerprintPattern(
        name="battery_status",
        pattern=re.compile(
            r'navigator\s*\.\s*getBattery\s*\(',
            re.IGNORECASE,
        ),
        severity=0.4,
        description="배터리 상태 API를 이용한 핑거프린팅",
    ),
]


# ============================================================
# 픽셀 추적기 패턴
# ============================================================

PIXEL_TRACKER_PATTERNS: list[re.Pattern] = [
    # 1x1 이미지 (픽셀 추적기)
    re.compile(
        r'<img[^>]*(?:width\s*=\s*["\']?1["\']?\s+height\s*=\s*["\']?1["\']?'
        r'|height\s*=\s*["\']?1["\']?\s+width\s*=\s*["\']?1["\']?)',
        re.IGNORECASE,
    ),
    # 빈 gif / 투명 gif
    re.compile(
        r'(?:src|href)\s*=\s*["\'][^"\']*(?:blank\.gif|pixel\.gif|'
        r'spacer\.gif|clear\.gif|track\.gif|beacon)',
        re.IGNORECASE,
    ),
    # navigator.sendBeacon
    re.compile(
        r'navigator\s*\.\s*sendBeacon\s*\(',
        re.IGNORECASE,
    ),
    # new Image() 추적
    re.compile(
        r'new\s+Image\s*\(\s*\)\s*\.\s*src\s*=',
        re.IGNORECASE,
    ),
]


class PrivacyAnalyzer:
    """
    프라이버시 위협 분석기

    추적기, 핑거프린팅, 써드파티 쿠키, 픽셀 추적기를 탐지하고
    종합 프라이버시 위험 점수를 산출합니다.
    """

    def __init__(self, config: AgentConfig) -> None:
        """
        프라이버시 분석기 초기화

        Args:
            config: 에이전트 전역 설정
        """
        self.config = config
        self._threshold = config.threats.privacy_threshold
        logger.debug("PrivacyAnalyzer 초기화 완료")

    # ============================
    # 공개 분석 메서드
    # ============================

    async def analyze_page(
        self, url: str, html_content: str
    ) -> Optional[ThreatDetail]:
        """
        웹 페이지 프라이버시 종합 분석

        추적기, 핑거프린팅, 쿠키, 픽셀을 모두 검사합니다.

        Args:
            url: 페이지 URL
            html_content: HTML 소스 코드

        Returns:
            ThreatDetail 또는 None (프라이버시 문제 없는 경우)
        """
        parsed = urlparse(url)
        page_domain = parsed.hostname or ""

        # 각 분석 실행
        tracker_result = self._analyze_trackers(html_content)
        fingerprint_result = self._analyze_fingerprinting(html_content)
        cookie_result = self._analyze_third_party_cookies(html_content, page_domain)
        pixel_result = self._analyze_pixel_trackers(html_content)
        exfil_result = self._analyze_data_exfiltration(html_content, page_domain)

        # 종합 점수 계산
        combined_score = self._calculate_combined_score(
            tracker_result, fingerprint_result,
            cookie_result, pixel_result, exfil_result,
        )

        if combined_score < self._threshold:
            return None

        threat_level = self._score_to_level(combined_score)

        # 모든 지표 수집
        indicators = (
            tracker_result["indicators"]
            + fingerprint_result["indicators"]
            + cookie_result["indicators"]
            + pixel_result["indicators"]
            + exfil_result["indicators"]
        )

        return ThreatDetail(
            threat_type=ThreatType.PRIVACY,
            threat_level=threat_level,
            confidence=combined_score,
            description=f"프라이버시 위협 탐지 (점수: {combined_score:.2f})",
            indicators=indicators,
            metadata={
                "tracker_count": tracker_result["count"],
                "tracker_domains": tracker_result["domains"],
                "fingerprint_apis": fingerprint_result["apis"],
                "fingerprint_score": fingerprint_result["score"],
                "third_party_cookie_domains": cookie_result["domains"],
                "pixel_tracker_count": pixel_result["count"],
                "data_exfiltration_risk": exfil_result["score"],
            },
        )

    async def check_url_tracker(self, url: str) -> Optional[str]:
        """
        URL이 알려진 추적기인지 확인

        Args:
            url: 검사할 URL

        Returns:
            매칭된 추적기 패턴명 또는 None
        """
        for i, pattern in enumerate(COMPILED_TRACKER_PATTERNS):
            if pattern.search(url):
                return TRACKER_PATTERNS[i]
        return None

    # ============================
    # 추적기 분석
    # ============================

    def _analyze_trackers(self, html_content: str) -> dict:
        """
        HTML 내 추적기 URL 매칭

        EasyList/EasyPrivacy 패턴에 기반하여 추적기를 탐지합니다.
        """
        # HTML에서 모든 URL 추출
        url_pattern = re.compile(
            r'(?:src|href|action|data-src)\s*=\s*["\']?(https?://[^"\'\s>]+)',
            re.IGNORECASE,
        )
        found_urls = url_pattern.findall(html_content)

        # 스크립트 내 URL도 추출
        script_url_pattern = re.compile(
            r'["\']+(https?://[^"\']+)["\']',
        )
        script_blocks = re.findall(
            r'<script[^>]*>(.*?)</script>',
            html_content, re.DOTALL | re.IGNORECASE,
        )
        for block in script_blocks:
            found_urls.extend(script_url_pattern.findall(block))

        # 추적기 매칭
        matched_domains: list[str] = []
        matched_count = 0
        indicators: list[str] = []

        seen_domains: set[str] = set()

        for url in found_urls:
            for tracker_pattern in COMPILED_TRACKER_PATTERNS:
                if tracker_pattern.search(url):
                    domain = urlparse(url).hostname or url
                    if domain not in seen_domains:
                        seen_domains.add(domain)
                        matched_domains.append(domain)
                        indicators.append(f"추적기 탐지: {domain}")
                    matched_count += 1
                    break

        return {
            "count": matched_count,
            "domains": matched_domains,
            "indicators": indicators,
            "score": min(1.0, matched_count / 10.0),
        }

    # ============================
    # 핑거프린팅 분석
    # ============================

    def _analyze_fingerprinting(self, html_content: str) -> dict:
        """
        브라우저 핑거프린팅 API 사용 탐지

        Canvas, WebGL, AudioContext, 폰트 열거, Navigator 속성 등을 검사합니다.
        """
        # 인라인 스크립트 추출
        scripts = re.findall(
            r'<script[^>]*>(.*?)</script>',
            html_content, re.DOTALL | re.IGNORECASE,
        )
        combined_script = "\n".join(scripts)

        detected_apis: list[str] = []
        indicators: list[str] = []
        total_severity = 0.0

        for fp_pattern in FINGERPRINT_PATTERNS:
            matches = fp_pattern.pattern.findall(combined_script)
            if matches:
                detected_apis.append(fp_pattern.name)
                indicators.append(
                    f"핑거프린팅 API: {fp_pattern.description} ({len(matches)}회)"
                )
                total_severity += fp_pattern.severity

        # 여러 핑거프린팅 API 동시 사용은 더 위험
        api_count = len(detected_apis)
        if api_count >= 3:
            indicators.append(
                f"⚠️ 다중 핑거프린팅 API 사용 ({api_count}종 — 고위험)"
            )
            total_severity *= 1.3

        # 점수 정규화 (0.0~1.0)
        score = min(1.0, total_severity / 3.0)

        return {
            "apis": detected_apis,
            "api_count": api_count,
            "indicators": indicators,
            "score": score,
        }

    # ============================
    # 써드파티 쿠키 분석
    # ============================

    def _analyze_third_party_cookies(
        self, html_content: str, page_domain: str
    ) -> dict:
        """
        써드파티 쿠키 설정 가능한 외부 리소스 분석

        페이지 도메인과 다른 도메인에서 로드되는 리소스를 탐지합니다.
        """
        # 외부 리소스 URL 추출
        resource_pattern = re.compile(
            r'(?:src|href)\s*=\s*["\']?(https?://([^/"\'\s>]+))',
            re.IGNORECASE,
        )
        matches = resource_pattern.findall(html_content)

        third_party_domains: set[str] = set()
        indicators: list[str] = []

        # 페이지 도메인의 기본 도메인 추출 (서브도메인 제거)
        base_domain = self._get_base_domain(page_domain)

        for _url, domain in matches:
            resource_base = self._get_base_domain(domain)
            if resource_base and resource_base != base_domain:
                third_party_domains.add(resource_base)

        domain_count = len(third_party_domains)
        if domain_count > 0:
            indicators.append(
                f"써드파티 도메인 {domain_count}개에서 리소스 로드"
            )

        if domain_count > 10:
            indicators.append(
                "⚠️ 과도한 써드파티 도메인 연결 (쿠키 추적 위험)"
            )

        # 쿠키 설정 코드 탐지
        cookie_set_pattern = re.compile(
            r'document\.cookie\s*=', re.IGNORECASE,
        )
        cookie_sets = len(cookie_set_pattern.findall(html_content))
        if cookie_sets > 0:
            indicators.append(f"JavaScript 쿠키 설정 {cookie_sets}회")

        score = min(1.0, domain_count / 15.0 + cookie_sets * 0.05)

        return {
            "domains": list(third_party_domains),
            "domain_count": domain_count,
            "cookie_sets": cookie_sets,
            "indicators": indicators,
            "score": score,
        }

    # ============================
    # 픽셀 추적기 분석
    # ============================

    def _analyze_pixel_trackers(self, html_content: str) -> dict:
        """
        1x1 픽셀 이미지 및 비콘 추적기 탐지

        투명 이미지, sendBeacon, Image() 추적 패턴을 검사합니다.
        """
        total_count = 0
        indicators: list[str] = []

        pixel_labels = [
            "1x1 이미지 (픽셀 추적기)",
            "빈/투명 GIF 추적기",
            "navigator.sendBeacon 비콘",
            "new Image() 추적",
        ]

        for i, pattern in enumerate(PIXEL_TRACKER_PATTERNS):
            matches = pattern.findall(html_content)
            count = len(matches)
            if count > 0:
                total_count += count
                indicators.append(
                    f"픽셀 추적: {pixel_labels[i]} ({count}회)"
                )

        score = min(1.0, total_count / 5.0)

        return {
            "count": total_count,
            "indicators": indicators,
            "score": score,
        }

    # ============================
    # 데이터 유출 위험 분석
    # ============================

    def _analyze_data_exfiltration(
        self, html_content: str, page_domain: str
    ) -> dict:
        """
        민감 데이터 수집 및 외부 전송 위험 분석

        폼 액션 URL, 데이터 수집 패턴, 외부 전송을 검사합니다.
        """
        indicators: list[str] = []
        risk_score = 0.0

        base_domain = self._get_base_domain(page_domain)

        # 1. 폼 액션이 외부 도메인인 경우
        form_action_pattern = re.compile(
            r'<form[^>]*action\s*=\s*["\']?(https?://([^/"\'\s>]+))',
            re.IGNORECASE,
        )
        form_actions = form_action_pattern.findall(html_content)
        external_forms = [
            domain for _, domain in form_actions
            if self._get_base_domain(domain) != base_domain
        ]
        if external_forms:
            indicators.append(
                f"외부 도메인으로 폼 데이터 전송: {', '.join(external_forms[:3])}"
            )
            risk_score += 0.3

        # 2. localStorage/sessionStorage 접근 후 외부 전송
        storage_exfil = re.findall(
            r'(?:localStorage|sessionStorage)\s*\.\s*(?:getItem|setItem)'
            r'[^;]*(?:fetch|XMLHttpRequest|sendBeacon)',
            html_content,
            re.IGNORECASE | re.DOTALL,
        )
        if storage_exfil:
            indicators.append("로컬 스토리지 데이터 외부 전송 시도")
            risk_score += 0.25

        # 3. document.cookie 접근 후 외부 전송
        cookie_exfil = re.findall(
            r'document\.cookie[^;]*(?:fetch|XMLHttpRequest|sendBeacon|new\s+Image)',
            html_content,
            re.IGNORECASE | re.DOTALL,
        )
        if cookie_exfil:
            indicators.append("쿠키 데이터 외부 전송 시도")
            risk_score += 0.3

        # 4. 위치 정보 수집
        geolocation = re.findall(
            r'navigator\s*\.\s*geolocation\s*\.\s*(?:getCurrentPosition|watchPosition)',
            html_content,
            re.IGNORECASE,
        )
        if geolocation:
            indicators.append("위치 정보 수집")
            risk_score += 0.15

        # 5. 미디어 장치 열거
        media_devices = re.findall(
            r'navigator\s*\.\s*mediaDevices\s*\.\s*enumerateDevices',
            html_content,
            re.IGNORECASE,
        )
        if media_devices:
            indicators.append("미디어 장치 목록 수집")
            risk_score += 0.10

        return {
            "indicators": indicators,
            "score": min(1.0, risk_score),
        }

    # ============================
    # 종합 점수 계산
    # ============================

    def _calculate_combined_score(
        self,
        tracker: dict,
        fingerprint: dict,
        cookie: dict,
        pixel: dict,
        exfil: dict,
    ) -> float:
        """
        모든 프라이버시 분석 결과를 종합한 점수 계산

        가중 평균으로 최종 프라이버시 위험 점수를 산출합니다.
        """
        weights = {
            "tracker": 0.25,
            "fingerprint": 0.30,
            "cookie": 0.15,
            "pixel": 0.10,
            "exfiltration": 0.20,
        }

        combined = (
            tracker["score"] * weights["tracker"]
            + fingerprint["score"] * weights["fingerprint"]
            + cookie["score"] * weights["cookie"]
            + pixel["score"] * weights["pixel"]
            + exfil["score"] * weights["exfiltration"]
        )

        return min(1.0, max(0.0, combined))

    # ============================
    # 유틸리티
    # ============================

    @staticmethod
    def _get_base_domain(domain: str) -> str:
        """
        도메인에서 기본 도메인 추출 (서브도메인 제거)

        예: 'sub.example.com' → 'example.com'
        """
        if not domain:
            return ""
        parts = domain.split(".")
        if len(parts) >= 2:
            return ".".join(parts[-2:])
        return domain

    @staticmethod
    def _score_to_level(score: float) -> ThreatLevel:
        """점수를 위협 수준으로 변환"""
        if score >= 0.85:
            return ThreatLevel.CRITICAL
        elif score >= 0.65:
            return ThreatLevel.HIGH
        elif score >= 0.45:
            return ThreatLevel.MEDIUM
        elif score >= 0.25:
            return ThreatLevel.LOW
        return ThreatLevel.SAFE
