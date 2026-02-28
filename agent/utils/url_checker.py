"""
URL 안전성 검사기
=================

외부 위협 인텔리전스 API를 활용한 URL 안전성 검증.

지원 API:
- Google Safe Browsing API v4 (threatMatches:find)
- VirusTotal API v3 (/urls 엔드포인트)

기능:
- 비동기 aiohttp 세션 관리
- 응답 캐싱 (1시간 TTL)
- 속도 제한 (asyncio.Semaphore)
- URL 정규화 (urllib.parse)
"""

from __future__ import annotations

import asyncio
import base64
import hashlib
import logging
import time
from typing import Any, Optional
from urllib.parse import urlparse, urlunparse, quote, unquote

import aiohttp

from agent.core.config import ExternalAPIConfig

logger = logging.getLogger(__name__)


# ============================================================
# 캐시 항목
# ============================================================

class _CacheEntry:
    """TTL 기반 응답 캐시 항목"""

    __slots__ = ("result", "created_at", "ttl")

    def __init__(self, result: dict[str, Any], created_at: float, ttl: float) -> None:
        self.result = result
        self.created_at = created_at
        self.ttl = ttl

    @property
    def is_expired(self) -> bool:
        return (time.time() - self.created_at) > self.ttl


# ============================================================
# URL 정규화
# ============================================================

def normalize_url(url: str) -> str:
    """
    URL 정규화

    - 스킴 소문자 변환
    - 호스트 소문자 변환
    - 기본 포트 제거 (80/443)
    - 경로 정규화 (연속 슬래시, 트레일링 슬래시)
    - 퍼센트 인코딩 정규화

    Args:
        url: 원본 URL

    Returns:
        정규화된 URL
    """
    # 스킴이 없으면 https 추가
    if not url.startswith(("http://", "https://", "ftp://")):
        url = "https://" + url

    parsed = urlparse(url)

    # 스킴/호스트 소문자
    scheme = parsed.scheme.lower()
    hostname = (parsed.hostname or "").lower()

    # 기본 포트 제거
    port = parsed.port
    if (scheme == "http" and port == 80) or (scheme == "https" and port == 443):
        port = None

    # netloc 재구성
    netloc = hostname
    if parsed.username:
        userinfo = parsed.username
        if parsed.password:
            userinfo += f":{parsed.password}"
        netloc = f"{userinfo}@{hostname}"
    if port is not None:
        netloc += f":{port}"

    # 경로 정규화: 디코딩 후 재인코딩 (일관성)
    path = unquote(parsed.path or "/")
    # 연속 슬래시 제거
    while "//" in path:
        path = path.replace("//", "/")
    # 빈 경로는 /
    if not path:
        path = "/"
    path = quote(path, safe="/:@!$&'()*+,;=-._~")

    # 쿼리 및 프래그먼트 유지
    query = parsed.query
    fragment = parsed.fragment

    return urlunparse((scheme, netloc, path, "", query, fragment))


# ============================================================
# URL 검사기
# ============================================================

class URLChecker:
    """
    URL 안전성 검사기

    Google Safe Browsing API v4와 VirusTotal API v3을 사용하여
    URL의 악성 여부를 비동기로 검사합니다.

    속도 제한, 응답 캐싱, URL 정규화를 지원합니다.
    """

    # 캐시 TTL (1시간)
    CACHE_TTL = 3600.0

    def __init__(self, config: Optional[ExternalAPIConfig] = None) -> None:
        """
        URL 검사기 초기화

        Args:
            config: 외부 API 설정. None이면 기본 설정 사용.
        """
        self.config = config or ExternalAPIConfig()

        # aiohttp 세션 (지연 초기화)
        self._session: Optional[aiohttp.ClientSession] = None

        # 응답 캐시 (URL 해시 → _CacheEntry)
        self._cache: dict[str, _CacheEntry] = {}
        self._cache_lock = asyncio.Lock()

        # 속도 제한 세마포어 (분당 요청 수 제한)
        self._rate_limiter = asyncio.Semaphore(
            self.config.rate_limit_per_minute
        )

        # API 사용 가능 여부
        self._gsb_available = bool(self.config.google_safe_browsing_key)
        self._vt_available = bool(self.config.virustotal_key)

        logger.info(
            "URLChecker 초기화 — GSB: %s, VT: %s",
            self._gsb_available, self._vt_available,
        )

    # ============================
    # 컨텍스트 매니저
    # ============================

    async def __aenter__(self) -> "URLChecker":
        """비동기 컨텍스트 매니저 진입"""
        await self._ensure_session()
        return self

    async def __aexit__(self, *args: Any) -> None:
        """비동기 컨텍스트 매니저 종료"""
        await self.close()

    async def close(self) -> None:
        """aiohttp 세션 종료 및 리소스 해제"""
        if self._session and not self._session.closed:
            await self._session.close()
            self._session = None
        logger.debug("URLChecker 세션 종료")

    # ============================
    # 공개 API
    # ============================

    async def check_url(self, url: str) -> dict[str, Any]:
        """
        URL 종합 안전성 검사

        Google Safe Browsing과 VirusTotal을 동시에 조회하고
        결과를 통합합니다.

        Args:
            url: 검사할 URL

        Returns:
            검사 결과 딕셔너리:
            {
                "url": 정규화된 URL,
                "safe": bool,
                "threats": [위협 목록],
                "google_safe_browsing": GSB 결과 또는 None,
                "virustotal": VT 결과 또는 None,
                "cached": bool,
            }
        """
        # URL 정규화
        normalized = normalize_url(url)

        # 캐시 확인
        cache_key = self._make_cache_key(normalized)
        cached = await self._get_cached(cache_key)
        if cached is not None:
            cached["cached"] = True
            return cached

        # Google Safe Browsing + VirusTotal 동시 조회
        tasks = []
        if self._gsb_available:
            tasks.append(self._check_google_safe_browsing(normalized))
        else:
            tasks.append(asyncio.coroutine(lambda: None)() if False else self._noop())

        if self._vt_available:
            tasks.append(self._check_virustotal(normalized))
        else:
            tasks.append(self._noop())

        gsb_result, vt_result = await asyncio.gather(*tasks, return_exceptions=True)

        # 예외 처리
        if isinstance(gsb_result, Exception):
            logger.error("Google Safe Browsing 오류: %s", gsb_result)
            gsb_result = None
        if isinstance(vt_result, Exception):
            logger.error("VirusTotal 오류: %s", vt_result)
            vt_result = None

        # 결과 통합
        threats: list[str] = []
        safe = True

        # GSB 결과 처리
        if gsb_result and gsb_result.get("threats"):
            threats.extend(gsb_result["threats"])
            safe = False

        # VT 결과 처리
        if vt_result and vt_result.get("malicious", 0) > 0:
            threats.append(
                f"VirusTotal: {vt_result['malicious']}개 엔진에서 악성 판정"
            )
            safe = False

        result: dict[str, Any] = {
            "url": normalized,
            "safe": safe,
            "threats": threats,
            "google_safe_browsing": gsb_result,
            "virustotal": vt_result,
            "cached": False,
        }

        # 캐시 저장
        await self._set_cached(cache_key, result)

        return result

    async def check_urls_batch(
        self, urls: list[str]
    ) -> list[dict[str, Any]]:
        """
        여러 URL 일괄 검사

        Args:
            urls: 검사할 URL 리스트

        Returns:
            검사 결과 리스트
        """
        tasks = [self.check_url(url) for url in urls]
        return await asyncio.gather(*tasks)

    # ============================
    # Google Safe Browsing API v4
    # ============================

    async def _check_google_safe_browsing(
        self, url: str
    ) -> Optional[dict[str, Any]]:
        """
        Google Safe Browsing API v4 threatMatches:find 호출

        Args:
            url: 검사할 URL

        Returns:
            검사 결과 또는 None
        """
        async with self._rate_limiter:
            session = await self._ensure_session()

            # API 요청 본문 구성
            payload = {
                "client": {
                    "clientId": "ordinalv8",
                    "clientVersion": "0.1.0",
                },
                "threatInfo": {
                    "threatTypes": [
                        "MALWARE",
                        "SOCIAL_ENGINEERING",
                        "UNWANTED_SOFTWARE",
                        "POTENTIALLY_HARMFUL_APPLICATION",
                    ],
                    "platformTypes": ["ANY_PLATFORM"],
                    "threatEntryTypes": ["URL"],
                    "threatEntries": [{"url": url}],
                },
            }

            api_url = (
                f"{self.config.google_safe_browsing_url}"
                f"?key={self.config.google_safe_browsing_key}"
            )

            try:
                async with session.post(
                    api_url,
                    json=payload,
                    timeout=aiohttp.ClientTimeout(
                        total=self.config.request_timeout
                    ),
                ) as response:
                    if response.status != 200:
                        logger.warning(
                            "GSB API 응답 코드 %d: %s",
                            response.status, await response.text(),
                        )
                        return None

                    data = await response.json()

                    # 위협 매칭 결과 파싱
                    matches = data.get("matches", [])
                    threats: list[str] = []
                    for match in matches:
                        threat_type = match.get("threatType", "UNKNOWN")
                        platform = match.get("platformType", "UNKNOWN")
                        threats.append(f"{threat_type} ({platform})")

                    return {
                        "threats": threats,
                        "match_count": len(matches),
                        "raw_matches": matches,
                    }

            except asyncio.TimeoutError:
                logger.warning("GSB API 타임아웃: %s", url)
                return None
            except Exception as e:
                logger.error("GSB API 호출 실패: %s", e)
                return None

    # ============================
    # VirusTotal API v3
    # ============================

    async def _check_virustotal(
        self, url: str
    ) -> Optional[dict[str, Any]]:
        """
        VirusTotal API v3 /urls 엔드포인트 호출

        URL을 Base64 인코딩하여 분석 결과를 조회합니다.

        Args:
            url: 검사할 URL

        Returns:
            검사 결과 또는 None
        """
        async with self._rate_limiter:
            session = await self._ensure_session()

            # URL ID: Base64(URL) without padding
            url_id = base64.urlsafe_b64encode(url.encode()).decode().rstrip("=")

            api_url = f"{self.config.virustotal_url}/urls/{url_id}"
            headers = {
                "x-apikey": self.config.virustotal_key,
                "Accept": "application/json",
            }

            try:
                async with session.get(
                    api_url,
                    headers=headers,
                    timeout=aiohttp.ClientTimeout(
                        total=self.config.request_timeout
                    ),
                ) as response:
                    if response.status == 404:
                        # URL이 VT DB에 없음 → 스캔 제출
                        return await self._submit_virustotal_scan(url)

                    if response.status != 200:
                        logger.warning(
                            "VT API 응답 코드 %d: %s",
                            response.status, await response.text(),
                        )
                        return None

                    data = await response.json()
                    attributes = data.get("data", {}).get("attributes", {})
                    stats = attributes.get("last_analysis_stats", {})

                    return {
                        "malicious": stats.get("malicious", 0),
                        "suspicious": stats.get("suspicious", 0),
                        "harmless": stats.get("harmless", 0),
                        "undetected": stats.get("undetected", 0),
                        "total_engines": sum(stats.values()) if stats else 0,
                        "reputation": attributes.get("reputation", 0),
                        "last_analysis_date": attributes.get(
                            "last_analysis_date"
                        ),
                    }

            except asyncio.TimeoutError:
                logger.warning("VT API 타임아웃: %s", url)
                return None
            except Exception as e:
                logger.error("VT API 호출 실패: %s", e)
                return None

    async def _submit_virustotal_scan(
        self, url: str
    ) -> Optional[dict[str, Any]]:
        """
        VirusTotal에 URL 스캔 제출

        DB에 없는 URL에 대해 새 스캔을 요청합니다.

        Args:
            url: 스캔할 URL

        Returns:
            제출 결과 또는 None
        """
        session = await self._ensure_session()

        api_url = f"{self.config.virustotal_url}/urls"
        headers = {
            "x-apikey": self.config.virustotal_key,
            "Content-Type": "application/x-www-form-urlencoded",
        }
        data = f"url={url}"

        try:
            async with session.post(
                api_url,
                headers=headers,
                data=data,
                timeout=aiohttp.ClientTimeout(
                    total=self.config.request_timeout
                ),
            ) as response:
                if response.status == 200:
                    result = await response.json()
                    analysis_id = (
                        result.get("data", {}).get("id", "unknown")
                    )
                    logger.info("VT 스캔 제출 완료: %s (ID: %s)", url, analysis_id)
                    return {
                        "malicious": 0,
                        "suspicious": 0,
                        "harmless": 0,
                        "undetected": 0,
                        "total_engines": 0,
                        "status": "submitted",
                        "analysis_id": analysis_id,
                    }
                else:
                    logger.warning(
                        "VT 스캔 제출 실패 (코드 %d)", response.status
                    )
                    return None

        except Exception as e:
            logger.error("VT 스캔 제출 오류: %s", e)
            return None

    # ============================
    # 세션 관리
    # ============================

    async def _ensure_session(self) -> aiohttp.ClientSession:
        """aiohttp 세션 보장 (없으면 생성)"""
        if self._session is None or self._session.closed:
            self._session = aiohttp.ClientSession(
                headers={"User-Agent": "OrdinalV8/2.0.0"},
            )
        return self._session

    # ============================
    # 캐시 관리
    # ============================

    def _make_cache_key(self, url: str) -> str:
        """URL 기반 캐시 키 생성"""
        return hashlib.sha256(url.encode("utf-8")).hexdigest()

    async def _get_cached(self, key: str) -> Optional[dict[str, Any]]:
        """캐시 조회"""
        async with self._cache_lock:
            entry = self._cache.get(key)
            if entry is None:
                return None
            if entry.is_expired:
                del self._cache[key]
                return None
            return entry.result.copy()

    async def _set_cached(self, key: str, result: dict[str, Any]) -> None:
        """캐시 저장"""
        async with self._cache_lock:
            # 캐시 크기 제한 (최대 10000개)
            if len(self._cache) >= 10000:
                # 만료된 항목 정리
                expired_keys = [
                    k for k, v in self._cache.items() if v.is_expired
                ]
                for k in expired_keys:
                    del self._cache[k]

                # 여전히 초과하면 가장 오래된 것 제거
                if len(self._cache) >= 10000:
                    oldest = next(iter(self._cache))
                    del self._cache[oldest]

            self._cache[key] = _CacheEntry(
                result=result,
                created_at=time.time(),
                ttl=self.CACHE_TTL,
            )

    async def clear_cache(self) -> None:
        """캐시 전체 삭제"""
        async with self._cache_lock:
            self._cache.clear()

    @property
    def cache_size(self) -> int:
        """현재 캐시 크기"""
        return len(self._cache)

    # ============================
    # 유틸리티
    # ============================

    @staticmethod
    async def _noop() -> None:
        """아무것도 하지 않는 코루틴 (플레이스홀더)"""
        return None

    def __repr__(self) -> str:
        return (
            f"URLChecker(gsb={self._gsb_available}, "
            f"vt={self._vt_available}, "
            f"cache={self.cache_size})"
        )
