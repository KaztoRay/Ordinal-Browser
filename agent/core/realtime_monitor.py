"""
실시간 페이지 모니터링 엔진

DOM 변이, 네트워크 요청, 크립토마이닝 탐지를 통한
실시간 보안 모니터링을 제공합니다.
"""

import asyncio
import re
import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional


class AlertSeverity(Enum):
    """알림 심각도"""
    INFO = "info"
    WARNING = "warning"
    DANGER = "danger"
    CRITICAL = "critical"


class AlertType(Enum):
    """알림 유형"""
    SCRIPT_INJECTION = "script_injection"
    IFRAME_INSERTION = "iframe_insertion"
    FORM_HIJACK = "form_hijack"
    SUSPICIOUS_REQUEST = "suspicious_request"
    CRYPTO_MINING = "crypto_mining"
    DATA_EXFILTRATION = "data_exfiltration"
    DOM_CLOBBERING = "dom_clobbering"
    EVAL_EXECUTION = "eval_execution"


@dataclass
class SecurityAlert:
    """보안 알림"""
    alert_type: AlertType
    severity: AlertSeverity
    page_id: str
    message: str
    details: dict = field(default_factory=dict)
    timestamp: float = field(default_factory=time.time)


@dataclass
class PageMonitorState:
    """페이지 모니터링 상태"""
    page_id: str
    is_active: bool = True
    alerts: list[SecurityAlert] = field(default_factory=list)
    anomaly_score: float = 0.0
    request_count: int = 0
    blocked_count: int = 0
    mutation_count: int = 0
    start_time: float = field(default_factory=time.time)


class RealtimeMonitor:
    """
    실시간 보안 모니터링 엔진

    페이지별로 DOM 변이, 네트워크 요청, 스크립트 실행을
    실시간으로 감시하고 위협을 탐지합니다.
    """

    # 크립토마이닝 패턴
    MINING_PATTERNS: list[re.Pattern] = [
        re.compile(r'coinhive\.min\.js', re.IGNORECASE),
        re.compile(r'CoinHive\.Anonymous', re.IGNORECASE),
        re.compile(r'coinimp\.com/scripts', re.IGNORECASE),
        re.compile(r'crypto-?loot\.com', re.IGNORECASE),
        re.compile(r'coin-?hive\.com', re.IGNORECASE),
        re.compile(r'jsecoin\.com', re.IGNORECASE),
        re.compile(r'miner\.start\s*\(', re.IGNORECASE),
        re.compile(r'CryptoNoter', re.IGNORECASE),
        re.compile(r'WebAssembly\.instantiate.*mining', re.IGNORECASE),
        re.compile(r'stratum\+tcp://', re.IGNORECASE),
        re.compile(r'wasm.*(?:hash|mine|crypto)', re.IGNORECASE),
    ]

    # 의심스러운 DOM 변이 패턴
    SUSPICIOUS_SCRIPT_PATTERNS: list[re.Pattern] = [
        re.compile(r'<script[^>]*src=["\'](?!https?://(?:cdn|ajax|apis)\.)', re.IGNORECASE),
        re.compile(r'document\.write\s*\(', re.IGNORECASE),
        re.compile(r'eval\s*\(', re.IGNORECASE),
        re.compile(r'Function\s*\(', re.IGNORECASE),
        re.compile(r'innerHTML\s*=.*<script', re.IGNORECASE),
    ]

    # 데이터 유출 패턴
    EXFILTRATION_PATTERNS: list[re.Pattern] = [
        re.compile(r'document\.cookie', re.IGNORECASE),
        re.compile(r'localStorage\.getItem', re.IGNORECASE),
        re.compile(r'navigator\.sendBeacon\s*\(', re.IGNORECASE),
        re.compile(r'new\s+Image\(\)\.src\s*=', re.IGNORECASE),
    ]

    def __init__(self, alert_callback=None, max_alerts_per_page: int = 100):
        """
        Args:
            alert_callback: 알림 발생 시 호출할 콜백 (async)
            max_alerts_per_page: 페이지당 최대 알림 수
        """
        self._pages: dict[str, PageMonitorState] = {}
        self._alert_callback = alert_callback
        self._max_alerts = max_alerts_per_page
        self._known_threats: set[str] = set()  # 알려진 위협 URL 캐시

    async def start_monitoring(self, page_id: str) -> None:
        """페이지 모니터링 시작"""
        if page_id in self._pages:
            self._pages[page_id].is_active = True
            return
        self._pages[page_id] = PageMonitorState(page_id=page_id)

    async def stop_monitoring(self, page_id: str) -> Optional[PageMonitorState]:
        """페이지 모니터링 중지 — 최종 상태 반환"""
        state = self._pages.get(page_id)
        if state:
            state.is_active = False
        return state

    def get_state(self, page_id: str) -> Optional[PageMonitorState]:
        """페이지 모니터링 상태 조회"""
        return self._pages.get(page_id)

    def get_all_alerts(self) -> list[SecurityAlert]:
        """모든 페이지의 알림 수집"""
        alerts = []
        for state in self._pages.values():
            alerts.extend(state.alerts)
        return sorted(alerts, key=lambda a: a.timestamp, reverse=True)

    async def on_dom_mutation(self, page_id: str, mutations: list[dict]) -> list[SecurityAlert]:
        """
        DOM 변이 이벤트 처리

        Args:
            page_id: 페이지 식별자
            mutations: DOM 변이 목록 [{type, target, added_nodes, removed_nodes, attribute_name, old_value}]

        Returns:
            발생한 알림 목록
        """
        state = self._pages.get(page_id)
        if not state or not state.is_active:
            return []

        alerts: list[SecurityAlert] = []
        state.mutation_count += len(mutations)

        for mutation in mutations:
            mut_type = mutation.get("type", "")
            added_nodes = mutation.get("added_nodes", [])

            # 스크립트 태그 주입 탐지
            for node in added_nodes:
                node_str = str(node)
                tag_name = node.get("tag", "").lower() if isinstance(node, dict) else ""

                if tag_name == "script" or "<script" in node_str.lower():
                    src = node.get("src", "") if isinstance(node, dict) else ""
                    alert = SecurityAlert(
                        alert_type=AlertType.SCRIPT_INJECTION,
                        severity=AlertSeverity.DANGER,
                        page_id=page_id,
                        message=f"스크립트 태그 동적 삽입 감지: {src or '인라인 스크립트'}",
                        details={"node": node_str[:500], "src": src}
                    )
                    alerts.append(alert)
                    state.anomaly_score += 0.3

                # iframe 삽입 탐지
                if tag_name == "iframe" or "<iframe" in node_str.lower():
                    src = node.get("src", "") if isinstance(node, dict) else ""
                    alert = SecurityAlert(
                        alert_type=AlertType.IFRAME_INSERTION,
                        severity=AlertSeverity.WARNING,
                        page_id=page_id,
                        message=f"iframe 동적 삽입 감지: {src or '소스 없음'}",
                        details={"src": src}
                    )
                    alerts.append(alert)
                    state.anomaly_score += 0.2

                # 의심스러운 스크립트 패턴
                for pattern in self.SUSPICIOUS_SCRIPT_PATTERNS:
                    if pattern.search(node_str):
                        alert = SecurityAlert(
                            alert_type=AlertType.DOM_CLOBBERING,
                            severity=AlertSeverity.WARNING,
                            page_id=page_id,
                            message=f"의심스러운 DOM 조작 패턴: {pattern.pattern[:60]}",
                            details={"pattern": pattern.pattern, "content": node_str[:300]}
                        )
                        alerts.append(alert)
                        state.anomaly_score += 0.15
                        break

            # form action 변경 탐지 (폼 하이재킹)
            if mut_type == "attributes" and mutation.get("attribute_name") == "action":
                target = mutation.get("target", {})
                if isinstance(target, dict) and target.get("tag", "").lower() == "form":
                    old_val = mutation.get("old_value", "")
                    new_val = target.get("action", "")
                    if old_val != new_val:
                        alert = SecurityAlert(
                            alert_type=AlertType.FORM_HIJACK,
                            severity=AlertSeverity.CRITICAL,
                            page_id=page_id,
                            message=f"폼 action 속성 변경 감지: {old_val} → {new_val}",
                            details={"old_action": old_val, "new_action": new_val}
                        )
                        alerts.append(alert)
                        state.anomaly_score += 0.5

        # 알림 저장 및 콜백
        for alert in alerts:
            await self._emit_alert(state, alert)

        return alerts

    async def on_network_request(self, page_id: str, request: dict) -> Optional[SecurityAlert]:
        """
        네트워크 요청 감시

        Args:
            page_id: 페이지 식별자
            request: {url, method, headers, body, resource_type}

        Returns:
            위협 발견 시 알림, 아니면 None
        """
        state = self._pages.get(page_id)
        if not state or not state.is_active:
            return None

        state.request_count += 1
        url = request.get("url", "")
        body = request.get("body", "")

        # 알려진 위협 URL 체크
        if url in self._known_threats:
            alert = SecurityAlert(
                alert_type=AlertType.SUSPICIOUS_REQUEST,
                severity=AlertSeverity.DANGER,
                page_id=page_id,
                message=f"알려진 위협 URL 요청 차단: {url[:100]}",
                details={"url": url, "method": request.get("method")}
            )
            state.blocked_count += 1
            await self._emit_alert(state, alert)
            return alert

        # 데이터 유출 패턴 체크 (POST body에 쿠키/로컬스토리지 데이터)
        if body:
            for pattern in self.EXFILTRATION_PATTERNS:
                if pattern.search(body):
                    alert = SecurityAlert(
                        alert_type=AlertType.DATA_EXFILTRATION,
                        severity=AlertSeverity.CRITICAL,
                        page_id=page_id,
                        message=f"데이터 유출 의심 요청: {url[:80]}",
                        details={"url": url, "pattern": pattern.pattern, "body_preview": body[:200]}
                    )
                    state.anomaly_score += 0.4
                    await self._emit_alert(state, alert)
                    return alert

        # 크립토마이닝 URL 패턴
        for pattern in self.MINING_PATTERNS:
            if pattern.search(url):
                alert = SecurityAlert(
                    alert_type=AlertType.CRYPTO_MINING,
                    severity=AlertSeverity.DANGER,
                    page_id=page_id,
                    message=f"크립토마이닝 스크립트 로딩 감지: {url[:100]}",
                    details={"url": url}
                )
                state.anomaly_score += 0.6
                await self._emit_alert(state, alert)
                return alert

        return None

    def detect_crypto_mining(self, js_code: str) -> bool:
        """
        JavaScript 코드에서 크립토마이닝 패턴 탐지

        Args:
            js_code: 분석할 JavaScript 코드

        Returns:
            마이닝 코드 발견 여부
        """
        for pattern in self.MINING_PATTERNS:
            if pattern.search(js_code):
                return True

        # WebAssembly 기반 마이닝 휴리스틱
        wasm_indicators = [
            "WebAssembly.instantiate" in js_code,
            "WebAssembly.compile" in js_code and ("hash" in js_code.lower() or "nonce" in js_code.lower()),
            "SharedArrayBuffer" in js_code and "Atomics" in js_code,
        ]
        if sum(wasm_indicators) >= 2:
            return True

        # Worker 기반 마이닝 (다수의 Worker 생성 + hash 관련 코드)
        worker_count = js_code.count("new Worker")
        if worker_count >= 4 and ("hash" in js_code.lower() or "nonce" in js_code.lower()):
            return True

        return False

    def add_known_threat(self, url: str) -> None:
        """알려진 위협 URL 추가"""
        self._known_threats.add(url)

    def remove_known_threat(self, url: str) -> None:
        """알려진 위협 URL 제거"""
        self._known_threats.discard(url)

    def get_page_summary(self, page_id: str) -> dict:
        """페이지 모니터링 요약"""
        state = self._pages.get(page_id)
        if not state:
            return {"error": "페이지를 찾을 수 없습니다"}

        duration = time.time() - state.start_time
        return {
            "page_id": page_id,
            "active": state.is_active,
            "duration_seconds": round(duration, 1),
            "anomaly_score": round(min(state.anomaly_score, 1.0), 3),
            "total_alerts": len(state.alerts),
            "critical_alerts": sum(1 for a in state.alerts if a.severity == AlertSeverity.CRITICAL),
            "requests": state.request_count,
            "blocked": state.blocked_count,
            "mutations": state.mutation_count,
        }

    async def _emit_alert(self, state: PageMonitorState, alert: SecurityAlert) -> None:
        """알림 저장 및 콜백 호출"""
        if len(state.alerts) < self._max_alerts:
            state.alerts.append(alert)

        if self._alert_callback:
            try:
                await self._alert_callback(alert)
            except Exception:
                pass  # 콜백 오류는 무시

    def cleanup(self, page_id: str) -> None:
        """페이지 모니터링 데이터 정리"""
        self._pages.pop(page_id, None)

    def cleanup_all(self) -> None:
        """모든 모니터링 데이터 정리"""
        self._pages.clear()
