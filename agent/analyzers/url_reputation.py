"""
URL 평판 분석기 — 다중 소스 기반 URL 위험도 종합 평가

VirusTotal, Google Safe Browsing, PhishTank, URLhaus 등 외부 API를 
비동기로 조회하여 URL의 종합 평판 점수를 산출.
도메인 등록 연한 확인, TTL 기반 캐싱, 배치 분석 지원.

© 2026 KaztoRay — MIT License
"""

import asyncio
import hashlib
import time
import logging
from dataclasses import dataclass, field
from typing import Optional
from urllib.parse import urlparse

import aiohttp

logger = logging.getLogger(__name__)


# ============================================================
# 데이터 클래스 정의
# ============================================================

@dataclass
class ReputationSource:
    """개별 소스의 평판 결과"""
    source: str           # 소스 이름 (virustotal, safebrowsing 등)
    is_malicious: bool    # 악성 여부
    confidence: float     # 신뢰도 (0.0 ~ 1.0)
    details: dict = field(default_factory=dict)  # 상세 정보
    error: Optional[str] = None  # 에러 발생 시 메시지


@dataclass
class ReputationScore:
    """URL 종합 평판 점수"""
    url: str              # 분석 대상 URL
    score: int            # 종합 점수 (0=위험 ~ 100=안전)
    sources: list         # ReputationSource 목록
    details: dict = field(default_factory=dict)  # 종합 상세 정보
    cached: bool = False  # 캐시 히트 여부
    analyzed_at: float = field(default_factory=time.time)  # 분석 시각


@dataclass
class DomainInfo:
    """도메인 WHOIS 정보"""
    domain: str              # 도메인 이름
    creation_date: Optional[str] = None   # 등록일
    expiration_date: Optional[str] = None  # 만료일
    registrar: Optional[str] = None       # 등록기관
    age_days: int = -1       # 등록 후 경과 일수 (-1=확인 불가)
    is_suspicious: bool = False  # 의심스러운 도메인 여부 (신규 등록 등)
    error: Optional[str] = None


# ============================================================
# 캐시 엔트리 (TTL 기반)
# ============================================================

@dataclass
class _CacheEntry:
    """TTL 기반 캐시 엔트리"""
    data: object
    expires_at: float


# ============================================================
# URL 평판 분석기
# ============================================================

class URLReputationAnalyzer:
    """
    다중 소스 기반 URL 평판 분석기.
    
    비동기 메서드로 VirusTotal, Safe Browsing, PhishTank, URLhaus를
    병렬 조회하고, 결과를 종합하여 0~100 점수를 산출.
    
    사용법:
        analyzer = URLReputationAnalyzer(
            virustotal_api_key="...",
            safebrowsing_api_key="..."
        )
        score = await analyzer.aggregate_reputation("https://example.com")
        print(f"점수: {score.score}/100")
    """

    # 캐시 기본 TTL (초)
    DEFAULT_CACHE_TTL = 3600  # 1시간

    def __init__(
        self,
        virustotal_api_key: str = "",
        safebrowsing_api_key: str = "",
        cache_ttl: int = DEFAULT_CACHE_TTL,
        timeout: int = 10,
    ):
        """
        분석기 초기화.
        
        Args:
            virustotal_api_key: VirusTotal API 키
            safebrowsing_api_key: Google Safe Browsing API 키
            cache_ttl: 캐시 유효 기간 (초)
            timeout: HTTP 요청 타임아웃 (초)
        """
        self._vt_key = virustotal_api_key
        self._sb_key = safebrowsing_api_key
        self._cache_ttl = cache_ttl
        self._timeout = aiohttp.ClientTimeout(total=timeout)

        # TTL 기반 캐시 딕셔너리 {url_hash: _CacheEntry}
        self._cache: dict[str, _CacheEntry] = {}

        logger.info("[URLReputation] 분석기 초기화 — TTL=%ds", cache_ttl)

    # ============================================================
    # 캐시 관리
    # ============================================================

    def _cache_key(self, url: str) -> str:
        """URL의 SHA-256 해시를 캐시 키로 사용"""
        return hashlib.sha256(url.encode()).hexdigest()[:16]

    def _get_cached(self, url: str) -> Optional[ReputationScore]:
        """캐시에서 결과 조회 (만료되었으면 None)"""
        key = self._cache_key(url)
        entry = self._cache.get(key)
        if entry and entry.expires_at > time.time():
            logger.debug("[URLReputation] 캐시 히트: %s", url[:60])
            result = entry.data
            result.cached = True
            return result
        if entry:
            # 만료된 엔트리 제거
            del self._cache[key]
        return None

    def _set_cached(self, url: str, result: ReputationScore):
        """결과를 캐시에 저장"""
        key = self._cache_key(url)
        self._cache[key] = _CacheEntry(
            data=result,
            expires_at=time.time() + self._cache_ttl
        )

    def clear_cache(self):
        """캐시 전체 삭제"""
        self._cache.clear()
        logger.info("[URLReputation] 캐시 전체 삭제")

    def _cleanup_expired(self):
        """만료된 캐시 엔트리 정리"""
        now = time.time()
        expired = [k for k, v in self._cache.items() if v.expires_at <= now]
        for k in expired:
            del self._cache[k]
        if expired:
            logger.debug("[URLReputation] 만료된 캐시 %d건 정리", len(expired))

    # ============================================================
    # VirusTotal 조회
    # ============================================================

    async def query_virustotal(self, url: str) -> dict:
        """
        VirusTotal API v3로 URL 스캔 결과 조회.
        
        Args:
            url: 분석 대상 URL
            
        Returns:
            dict: {malicious: int, suspicious: int, harmless: int, 
                   undetected: int, total: int, permalink: str}
        """
        if not self._vt_key:
            logger.warning("[VirusTotal] API 키 미설정")
            return {"error": "API 키 미설정", "malicious": 0, "total": 0}

        # URL ID = base64url(URL)
        import base64
        url_id = base64.urlsafe_b64encode(url.encode()).decode().rstrip("=")

        api_url = f"https://www.virustotal.com/api/v3/urls/{url_id}"
        headers = {"x-apikey": self._vt_key}

        try:
            async with aiohttp.ClientSession(timeout=self._timeout) as session:
                async with session.get(api_url, headers=headers) as resp:
                    if resp.status == 404:
                        # 아직 스캔되지 않은 URL → 스캔 요청
                        logger.info("[VirusTotal] URL 미등록, 스캔 요청: %s", url[:60])
                        return {"malicious": 0, "total": 0, "status": "not_found"}

                    if resp.status != 200:
                        return {"error": f"HTTP {resp.status}", "malicious": 0, "total": 0}

                    data = await resp.json()
                    stats = data.get("data", {}).get("attributes", {}).get(
                        "last_analysis_stats", {})

                    return {
                        "malicious":  stats.get("malicious", 0),
                        "suspicious": stats.get("suspicious", 0),
                        "harmless":   stats.get("harmless", 0),
                        "undetected": stats.get("undetected", 0),
                        "total":      sum(stats.values()) if stats else 0,
                        "permalink":  f"https://www.virustotal.com/gui/url/{url_id}",
                    }

        except asyncio.TimeoutError:
            logger.error("[VirusTotal] 타임아웃: %s", url[:60])
            return {"error": "타임아웃", "malicious": 0, "total": 0}
        except Exception as e:
            logger.error("[VirusTotal] 오류: %s — %s", url[:60], str(e))
            return {"error": str(e), "malicious": 0, "total": 0}

    # ============================================================
    # Google Safe Browsing 조회
    # ============================================================

    async def query_safe_browsing(self, url: str) -> dict:
        """
        Google Safe Browsing Lookup API v4 조회.
        
        Args:
            url: 분석 대상 URL
            
        Returns:
            dict: {is_threat: bool, threat_types: list, platforms: list}
        """
        if not self._sb_key:
            logger.warning("[SafeBrowsing] API 키 미설정")
            return {"error": "API 키 미설정", "is_threat": False}

        api_url = (
            f"https://safebrowsing.googleapis.com/v4/threatMatches:find"
            f"?key={self._sb_key}"
        )

        payload = {
            "client": {
                "clientId": "ordinal-browser",
                "clientVersion": "1.1.0"
            },
            "threatInfo": {
                "threatTypes": [
                    "MALWARE",
                    "SOCIAL_ENGINEERING",
                    "UNWANTED_SOFTWARE",
                    "POTENTIALLY_HARMFUL_APPLICATION"
                ],
                "platformTypes": ["ANY_PLATFORM"],
                "threatEntryTypes": ["URL"],
                "threatEntries": [{"url": url}]
            }
        }

        try:
            async with aiohttp.ClientSession(timeout=self._timeout) as session:
                async with session.post(api_url, json=payload) as resp:
                    if resp.status != 200:
                        return {"error": f"HTTP {resp.status}", "is_threat": False}

                    data = await resp.json()
                    matches = data.get("matches", [])

                    if not matches:
                        return {"is_threat": False, "threat_types": [], "platforms": []}

                    threat_types = list({m.get("threatType", "") for m in matches})
                    platforms = list({m.get("platformType", "") for m in matches})

                    return {
                        "is_threat": True,
                        "threat_types": threat_types,
                        "platforms": platforms,
                        "match_count": len(matches)
                    }

        except asyncio.TimeoutError:
            logger.error("[SafeBrowsing] 타임아웃: %s", url[:60])
            return {"error": "타임아웃", "is_threat": False}
        except Exception as e:
            logger.error("[SafeBrowsing] 오류: %s — %s", url[:60], str(e))
            return {"error": str(e), "is_threat": False}

    # ============================================================
    # PhishTank 조회
    # ============================================================

    async def query_phishtank(self, url: str) -> dict:
        """
        PhishTank API로 피싱 URL 여부 확인.
        
        Args:
            url: 분석 대상 URL
            
        Returns:
            dict: {is_phishing: bool, phish_id: str, verified: bool}
        """
        api_url = "https://checkurl.phishtank.com/checkurl/"

        try:
            async with aiohttp.ClientSession(timeout=self._timeout) as session:
                form_data = aiohttp.FormData()
                form_data.add_field("url", url)
                form_data.add_field("format", "json")
                form_data.add_field("app_key", "ordinal-browser")

                async with session.post(api_url, data=form_data) as resp:
                    if resp.status != 200:
                        return {"error": f"HTTP {resp.status}", "is_phishing": False}

                    data = await resp.json()
                    results = data.get("results", {})

                    return {
                        "is_phishing": results.get("in_database", False)
                                       and results.get("valid", False),
                        "phish_id":    str(results.get("phish_id", "")),
                        "verified":    results.get("verified", False),
                        "verified_at": results.get("verified_at", ""),
                    }

        except asyncio.TimeoutError:
            logger.error("[PhishTank] 타임아웃: %s", url[:60])
            return {"error": "타임아웃", "is_phishing": False}
        except Exception as e:
            logger.error("[PhishTank] 오류: %s — %s", url[:60], str(e))
            return {"error": str(e), "is_phishing": False}

    # ============================================================
    # URLhaus 조회
    # ============================================================

    async def query_urlhaus(self, url: str) -> dict:
        """
        abuse.ch URLhaus API로 악성코드 배포 URL 확인.
        
        Args:
            url: 분석 대상 URL
            
        Returns:
            dict: {is_malware: bool, threat: str, tags: list, status: str}
        """
        api_url = "https://urlhaus-api.abuse.ch/v1/url/"

        try:
            async with aiohttp.ClientSession(timeout=self._timeout) as session:
                async with session.post(api_url, data={"url": url}) as resp:
                    if resp.status != 200:
                        return {"error": f"HTTP {resp.status}", "is_malware": False}

                    data = await resp.json()
                    query_status = data.get("query_status", "")

                    if query_status == "no_results":
                        return {"is_malware": False, "threat": "", "tags": []}

                    return {
                        "is_malware":  True,
                        "threat":      data.get("threat", "unknown"),
                        "tags":        data.get("tags", []),
                        "status":      data.get("url_status", ""),
                        "date_added":  data.get("date_added", ""),
                        "urlhaus_ref": data.get("urlhaus_reference", ""),
                    }

        except asyncio.TimeoutError:
            logger.error("[URLhaus] 타임아웃: %s", url[:60])
            return {"error": "타임아웃", "is_malware": False}
        except Exception as e:
            logger.error("[URLhaus] 오류: %s — %s", url[:60], str(e))
            return {"error": str(e), "is_malware": False}

    # ============================================================
    # 도메인 연한 확인 (python-whois)
    # ============================================================

    async def check_domain_age(self, domain: str) -> DomainInfo:
        """
        python-whois를 사용하여 도메인 등록 연한 확인.
        
        30일 미만 신규 도메인은 의심 플래그 설정.
        
        Args:
            domain: 도메인 이름 (예: "example.com")
            
        Returns:
            DomainInfo: 도메인 정보 (등록일, 만료일, 경과 일수)
        """
        try:
            import whois
            from datetime import datetime

            # WHOIS 조회 (동기 → 비동기 래핑)
            loop = asyncio.get_event_loop()
            w = await loop.run_in_executor(None, whois.whois, domain)

            creation = w.creation_date
            expiration = w.expiration_date

            # 리스트인 경우 첫 번째 값 사용
            if isinstance(creation, list):
                creation = creation[0]
            if isinstance(expiration, list):
                expiration = expiration[0]

            # 경과 일수 계산
            age_days = -1
            is_suspicious = False
            if creation and isinstance(creation, datetime):
                age_days = (datetime.now() - creation).days
                # 30일 미만 신규 도메인은 의심
                is_suspicious = age_days < 30

            return DomainInfo(
                domain=domain,
                creation_date=str(creation) if creation else None,
                expiration_date=str(expiration) if expiration else None,
                registrar=w.registrar,
                age_days=age_days,
                is_suspicious=is_suspicious,
            )

        except ImportError:
            logger.warning("[DomainAge] python-whois 미설치")
            return DomainInfo(domain=domain, error="python-whois 미설치")
        except Exception as e:
            logger.error("[DomainAge] 오류: %s — %s", domain, str(e))
            return DomainInfo(domain=domain, error=str(e))

    # ============================================================
    # 종합 평판 분석
    # ============================================================

    async def aggregate_reputation(self, url: str) -> ReputationScore:
        """
        모든 소스를 병렬 조회하여 종합 평판 점수 산출.
        
        점수 계산 로직:
          - 기본 100점에서 시작
          - VirusTotal 악성 탐지: 탐지 비율 * 40점 차감
          - Safe Browsing 위협: 30점 차감
          - PhishTank 피싱: 25점 차감
          - URLhaus 악성코드: 30점 차감
          - 신규 도메인 (30일 미만): 10점 차감
        
        Args:
            url: 분석 대상 URL
            
        Returns:
            ReputationScore: 종합 평판 결과 (0~100)
        """
        # 캐시 확인
        cached = self._get_cached(url)
        if cached:
            return cached

        logger.info("[URLReputation] 종합 분석 시작: %s", url[:80])

        # 도메인 추출
        parsed = urlparse(url)
        domain = parsed.netloc or parsed.path

        # 모든 소스 병렬 조회
        vt_task = self.query_virustotal(url)
        sb_task = self.query_safe_browsing(url)
        pt_task = self.query_phishtank(url)
        uh_task = self.query_urlhaus(url)
        da_task = self.check_domain_age(domain)

        vt_result, sb_result, pt_result, uh_result, domain_info = \
            await asyncio.gather(vt_task, sb_task, pt_task, uh_task, da_task)

        # 소스별 결과 정리
        sources = []
        score = 100  # 기본 점수

        # VirusTotal 점수 계산
        vt_malicious = vt_result.get("malicious", 0)
        vt_total = vt_result.get("total", 0)
        vt_ratio = vt_malicious / max(vt_total, 1)
        vt_deduction = int(vt_ratio * 40)
        score -= vt_deduction

        sources.append(ReputationSource(
            source="virustotal",
            is_malicious=vt_malicious > 0,
            confidence=vt_ratio,
            details=vt_result,
            error=vt_result.get("error"),
        ))

        # Safe Browsing 점수 계산
        sb_is_threat = sb_result.get("is_threat", False)
        if sb_is_threat:
            score -= 30

        sources.append(ReputationSource(
            source="safe_browsing",
            is_malicious=sb_is_threat,
            confidence=1.0 if sb_is_threat else 0.0,
            details=sb_result,
            error=sb_result.get("error"),
        ))

        # PhishTank 점수 계산
        pt_is_phishing = pt_result.get("is_phishing", False)
        if pt_is_phishing:
            score -= 25

        sources.append(ReputationSource(
            source="phishtank",
            is_malicious=pt_is_phishing,
            confidence=1.0 if pt_is_phishing else 0.0,
            details=pt_result,
            error=pt_result.get("error"),
        ))

        # URLhaus 점수 계산
        uh_is_malware = uh_result.get("is_malware", False)
        if uh_is_malware:
            score -= 30

        sources.append(ReputationSource(
            source="urlhaus",
            is_malicious=uh_is_malware,
            confidence=1.0 if uh_is_malware else 0.0,
            details=uh_result,
            error=uh_result.get("error"),
        ))

        # 도메인 연한 점수 조정
        if domain_info.is_suspicious:
            score -= 10

        # 점수 범위 제한 (0~100)
        score = max(0, min(100, score))

        # 종합 결과 생성
        result = ReputationScore(
            url=url,
            score=score,
            sources=sources,
            details={
                "domain_info": {
                    "domain": domain_info.domain,
                    "age_days": domain_info.age_days,
                    "registrar": domain_info.registrar,
                    "is_suspicious": domain_info.is_suspicious,
                },
                "threat_summary": {
                    "virustotal_detections": vt_malicious,
                    "safe_browsing_threat": sb_is_threat,
                    "phishtank_phishing": pt_is_phishing,
                    "urlhaus_malware": uh_is_malware,
                },
            },
        )

        # 캐시 저장
        self._set_cached(url, result)

        logger.info("[URLReputation] 분석 완료: %s → 점수 %d/100", url[:60], score)
        return result

    # ============================================================
    # 배치 분석
    # ============================================================

    async def batch_analyze(self, urls: list[str]) -> list[ReputationScore]:
        """
        여러 URL을 동시에 분석.
        
        Args:
            urls: URL 목록
            
        Returns:
            list[ReputationScore]: 각 URL의 평판 점수 목록
        """
        logger.info("[URLReputation] 배치 분석 시작: %d건", len(urls))

        # 만료 캐시 정리
        self._cleanup_expired()

        tasks = [self.aggregate_reputation(url) for url in urls]
        results = await asyncio.gather(*tasks, return_exceptions=True)

        # 예외 발생한 항목은 점수 0으로 처리
        processed = []
        for url, result in zip(urls, results):
            if isinstance(result, Exception):
                logger.error("[URLReputation] 배치 분석 오류: %s — %s", url[:60], str(result))
                processed.append(ReputationScore(
                    url=url, score=0, sources=[],
                    details={"error": str(result)}
                ))
            else:
                processed.append(result)

        logger.info("[URLReputation] 배치 분석 완료: %d건", len(processed))
        return processed
