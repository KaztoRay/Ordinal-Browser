"""
gRPC 보안 서비스 서버
=====================

비동기 gRPC 서버로 SecurityService를 제공합니다.

서비스 메서드:
- AnalyzeUrl: URL 보안 분석 → ThreatReport
- AnalyzeScript: JavaScript 코드 분석 → MalwareReport
- AnalyzePage: 페이지 종합 분석 → PageSecurityReport
- GetSecurityReport: 집계 보고서 조회
- StreamThreats: 실시간 위협 알림 (서버 스트리밍)
- HealthCheck: 서버 상태 확인

gRPC 서버는 graceful shutdown을 지원합니다.
"""

from __future__ import annotations

import asyncio
import logging
import signal
import time
from typing import Any, AsyncIterator, Optional

import grpc
from grpc import aio as grpc_aio

from agent.core.agent import (
    SecurityAgent,
    ThreatDetail,
    ThreatLevel,
    ThreatReport,
    ThreatType,
)
from agent.core.config import AgentConfig

logger = logging.getLogger(__name__)


# ============================================================
# 메시지 클래스 (Proto 생성 코드 대체)
# ============================================================
# 실제 proto 컴파일 전까지 Python 딕셔너리 기반으로 동작합니다.
# proto 컴파일 후에는 생성된 pb2 모듈을 임포트하여 교체합니다.

class _ThreatLevelProto:
    """Proto ThreatLevel 열거형 매핑"""
    SAFE = 0
    LOW = 1
    MEDIUM = 2
    HIGH = 3
    CRITICAL = 4

    @staticmethod
    def from_internal(level: ThreatLevel) -> int:
        """내부 ThreatLevel → Proto 값"""
        mapping = {
            ThreatLevel.SAFE: 0,
            ThreatLevel.LOW: 1,
            ThreatLevel.MEDIUM: 2,
            ThreatLevel.HIGH: 3,
            ThreatLevel.CRITICAL: 4,
        }
        return mapping.get(level, 0)


class _ThreatTypeProto:
    """Proto ThreatType 열거형 매핑"""
    PHISHING = 0
    MALWARE = 1
    XSS = 2
    PRIVACY = 3
    CERT = 4

    @staticmethod
    def from_internal(threat_type: ThreatType) -> int:
        """내부 ThreatType → Proto 값"""
        mapping = {
            ThreatType.PHISHING: 0,
            ThreatType.MALWARE: 1,
            ThreatType.XSS: 2,
            ThreatType.PRIVACY: 3,
            ThreatType.CERT: 4,
        }
        return mapping.get(threat_type, 0)


def _detail_to_dict(detail: ThreatDetail) -> dict[str, Any]:
    """ThreatDetail → Proto 호환 딕셔너리 변환"""
    return {
        "threat_type": _ThreatTypeProto.from_internal(detail.threat_type),
        "threat_level": _ThreatLevelProto.from_internal(detail.threat_level),
        "confidence": detail.confidence,
        "description": detail.description,
        "indicators": detail.indicators,
        "metadata": detail.metadata,
    }


def _report_to_dict(report: ThreatReport) -> dict[str, Any]:
    """ThreatReport → Proto 호환 딕셔너리 변환"""
    return {
        "url": report.url,
        "overall_level": _ThreatLevelProto.from_internal(report.overall_level),
        "overall_score": report.overall_score,
        "details": [_detail_to_dict(d) for d in report.details],
        "recommendations": report.recommendations,
        "analysis_time_ms": report.analysis_time_ms,
        "cached": report.cached,
        "timestamp": report.timestamp,
    }


# ============================================================
# gRPC SecurityService 서비서
# ============================================================

class SecurityServicer:
    """
    gRPC SecurityService 구현

    모든 RPC 메서드는 비동기로 동작하며,
    SecurityAgent를 사용하여 분석을 수행합니다.
    """

    def __init__(self, agent: SecurityAgent) -> None:
        """
        서비서 초기화

        Args:
            agent: 초기화된 SecurityAgent 인스턴스
        """
        self._agent = agent
        # 실시간 위협 스트리밍을 위한 큐
        self._threat_queues: list[asyncio.Queue[dict[str, Any]]] = []
        self._threat_queue_lock = asyncio.Lock()
        # 집계 보고서 저장
        self._reports: list[dict[str, Any]] = []
        self._reports_lock = asyncio.Lock()
        self._max_reports = 1000
        logger.info("SecurityServicer 초기화 완료")

    # ============================
    # AnalyzeUrl — URL 보안 분석
    # ============================

    async def AnalyzeUrl(
        self,
        request: dict[str, Any],
        context: Optional[Any] = None,
    ) -> dict[str, Any]:
        """
        URL 보안 분석

        Args:
            request: {"url": str, "use_llm": bool}
            context: gRPC 컨텍스트

        Returns:
            ThreatReport 딕셔너리
        """
        url = request.get("url", "")
        use_llm = request.get("use_llm", True)

        if not url:
            if context:
                context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
                context.set_details("URL이 비어 있습니다")
            return {"error": "URL이 비어 있습니다"}

        logger.info("AnalyzeUrl 요청: %s", url)

        try:
            report = await self._agent.analyze_url(url, use_llm=use_llm)
            result = _report_to_dict(report)

            # 보고서 저장 및 스트리밍
            await self._store_report(result)
            await self._broadcast_threat(result)

            return result

        except Exception as e:
            logger.error("AnalyzeUrl 오류: %s", e)
            if context:
                context.set_code(grpc.StatusCode.INTERNAL)
                context.set_details(str(e))
            return {"error": str(e)}

    # ============================
    # AnalyzeScript — JavaScript 분석
    # ============================

    async def AnalyzeScript(
        self,
        request: dict[str, Any],
        context: Optional[Any] = None,
    ) -> dict[str, Any]:
        """
        JavaScript 코드 보안 분석

        Args:
            request: {"code": str, "source_url": str}
            context: gRPC 컨텍스트

        Returns:
            MalwareReport (ThreatReport 딕셔너리)
        """
        code = request.get("code", "")
        source_url = request.get("source_url", "")

        if not code:
            if context:
                context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
                context.set_details("코드가 비어 있습니다")
            return {"error": "코드가 비어 있습니다"}

        logger.info("AnalyzeScript 요청: %d bytes (출처: %s)", len(code), source_url)

        try:
            report = await self._agent.analyze_script(code, source_url=source_url)
            result = _report_to_dict(report)

            await self._store_report(result)
            await self._broadcast_threat(result)

            return result

        except Exception as e:
            logger.error("AnalyzeScript 오류: %s", e)
            if context:
                context.set_code(grpc.StatusCode.INTERNAL)
                context.set_details(str(e))
            return {"error": str(e)}

    # ============================
    # AnalyzePage — 페이지 종합 분석
    # ============================

    async def AnalyzePage(
        self,
        request: dict[str, Any],
        context: Optional[Any] = None,
    ) -> dict[str, Any]:
        """
        웹 페이지 종합 보안 분석

        Args:
            request: {"url": str, "html_content": str, "use_llm": bool}
            context: gRPC 컨텍스트

        Returns:
            PageSecurityReport 딕셔너리
        """
        url = request.get("url", "")
        html_content = request.get("html_content", "")
        use_llm = request.get("use_llm", True)

        if not url or not html_content:
            if context:
                context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
                context.set_details("URL과 HTML 콘텐츠가 필요합니다")
            return {"error": "URL과 HTML 콘텐츠가 필요합니다"}

        logger.info("AnalyzePage 요청: %s (%d bytes)", url, len(html_content))

        try:
            report = await self._agent.analyze_page(
                url, html_content, use_llm=use_llm
            )
            result = _report_to_dict(report)

            # 페이지 보고서에 추가 정보
            result["page_url"] = url
            result["content_size"] = len(html_content)

            # 보안 점수 계산 (100점 만점)
            security_score = max(0, 100 - int(report.overall_score * 100))
            result["security_score"] = security_score

            await self._store_report(result)
            await self._broadcast_threat(result)

            return result

        except Exception as e:
            logger.error("AnalyzePage 오류: %s", e)
            if context:
                context.set_code(grpc.StatusCode.INTERNAL)
                context.set_details(str(e))
            return {"error": str(e)}

    # ============================
    # GetSecurityReport — 집계 보고서
    # ============================

    async def GetSecurityReport(
        self,
        request: dict[str, Any],
        context: Optional[Any] = None,
    ) -> dict[str, Any]:
        """
        최근 분석 결과 집계 보고서

        Args:
            request: {"limit": int, "min_level": int}
            context: gRPC 컨텍스트

        Returns:
            집계 보고서 딕셔너리
        """
        limit = request.get("limit", 50)
        min_level = request.get("min_level", 0)

        async with self._reports_lock:
            # 최소 위협 수준 필터링
            filtered = [
                r for r in self._reports
                if r.get("overall_level", 0) >= min_level
            ]
            # 최신순 정렬 후 limit 적용
            recent = sorted(
                filtered,
                key=lambda r: r.get("timestamp", 0),
                reverse=True,
            )[:limit]

        # 통계 계산
        total = len(self._reports)
        safe_count = sum(1 for r in self._reports if r.get("overall_level", 0) == 0)
        threat_count = total - safe_count

        return {
            "reports": recent,
            "total_analyzed": total,
            "safe_count": safe_count,
            "threat_count": threat_count,
            "cache_size": self._agent.cache_size,
        }

    # ============================
    # StreamThreats — 실시간 위협 스트리밍
    # ============================

    async def StreamThreats(
        self,
        request: dict[str, Any],
        context: Optional[Any] = None,
    ) -> AsyncIterator[dict[str, Any]]:
        """
        실시간 위협 알림 서버 스트리밍

        클라이언트가 연결하면 새로운 위협이 탐지될 때마다
        스트리밍으로 전송합니다.

        Args:
            request: {"min_level": int}
            context: gRPC 컨텍스트

        Yields:
            ThreatReport 딕셔너리 (실시간)
        """
        min_level = request.get("min_level", 1)  # 기본: LOW 이상만

        # 새 큐 등록
        queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue(maxsize=100)
        async with self._threat_queue_lock:
            self._threat_queues.append(queue)

        logger.info("StreamThreats 연결 시작 (min_level=%d)", min_level)

        try:
            while True:
                # 큐에서 위협 보고서 대기
                report = await queue.get()

                # 최소 위협 수준 필터링
                if report.get("overall_level", 0) >= min_level:
                    yield report

        except asyncio.CancelledError:
            logger.info("StreamThreats 연결 종료")
        finally:
            # 큐 등록 해제
            async with self._threat_queue_lock:
                if queue in self._threat_queues:
                    self._threat_queues.remove(queue)

    # ============================
    # HealthCheck — 서버 상태 확인
    # ============================

    async def HealthCheck(
        self,
        request: dict[str, Any],
        context: Optional[Any] = None,
    ) -> dict[str, Any]:
        """
        서버 헬스 체크

        Returns:
            서버 상태 정보
        """
        return {
            "status": "SERVING",
            "agent_initialized": self._agent.is_initialized,
            "cache_size": self._agent.cache_size,
            "total_reports": len(self._reports),
            "active_streams": len(self._threat_queues),
            "timestamp": time.time(),
            "version": self._agent.config.version,
        }

    # ============================
    # 내부 메서드
    # ============================

    async def _store_report(self, report: dict[str, Any]) -> None:
        """분석 보고서 저장"""
        async with self._reports_lock:
            self._reports.append(report)
            # 최대 개수 초과 시 가장 오래된 항목 제거
            if len(self._reports) > self._max_reports:
                self._reports = self._reports[-self._max_reports:]

    async def _broadcast_threat(self, report: dict[str, Any]) -> None:
        """모든 스트리밍 구독자에게 위협 알림 전송"""
        if report.get("overall_level", 0) == 0:
            return  # SAFE는 브로드캐스트하지 않음

        async with self._threat_queue_lock:
            for queue in self._threat_queues:
                try:
                    queue.put_nowait(report)
                except asyncio.QueueFull:
                    # 큐가 가득 차면 가장 오래된 항목 제거
                    try:
                        queue.get_nowait()
                        queue.put_nowait(report)
                    except (asyncio.QueueEmpty, asyncio.QueueFull):
                        pass


# ============================================================
# gRPC 서버 관리
# ============================================================

class _GrpcServer:
    """
    gRPC 서버 래퍼

    서버 시작, 종료, 시그널 핸들링을 관리합니다.
    """

    def __init__(self, config: AgentConfig) -> None:
        """
        서버 초기화

        Args:
            config: 에이전트 설정
        """
        self.config = config
        self._server: Optional[grpc_aio.Server] = None
        self._agent: Optional[SecurityAgent] = None
        self._servicer: Optional[SecurityServicer] = None
        self._shutdown_event = asyncio.Event()

    async def start(self) -> None:
        """
        gRPC 서버 시작

        SecurityAgent를 초기화하고 gRPC 서비스를 등록합니다.
        """
        logger.info("gRPC 서버 시작 준비 중...")

        # SecurityAgent 초기화
        self._agent = SecurityAgent(self.config)
        await self._agent.initialize()

        # 서비서 생성
        self._servicer = SecurityServicer(self._agent)

        # gRPC 서버 설정
        self._server = grpc_aio.server(
            options=[
                ("grpc.max_send_message_length", self.config.server.max_message_size),
                ("grpc.max_receive_message_length", self.config.server.max_message_size),
                ("grpc.keepalive_time_ms", 30000),
                ("grpc.keepalive_timeout_ms", 10000),
                ("grpc.http2.min_ping_interval_without_data_ms", 30000),
            ],
        )

        # 서비스 등록 (proto 컴파일 후 add_SecurityServiceServicer_to_server 사용)
        # 현재는 수동 등록
        listen_addr = f"{self.config.server.host}:{self.config.server.port}"
        self._server.add_insecure_port(listen_addr)

        await self._server.start()
        logger.info("gRPC 서버 시작됨: %s", listen_addr)

    async def stop(self) -> None:
        """gRPC 서버 graceful 종료"""
        if self._server:
            logger.info("gRPC 서버 종료 중...")
            # 5초 유예 기간
            await self._server.stop(grace=5.0)
            logger.info("gRPC 서버 종료 완료")

        if self._agent:
            await self._agent.shutdown()

    async def wait_for_termination(self) -> None:
        """서버 종료 대기"""
        if self._server:
            await self._server.wait_for_termination()

    @property
    def servicer(self) -> Optional[SecurityServicer]:
        """서비서 인스턴스"""
        return self._servicer


def _setup_signal_handlers(server: _GrpcServer, loop: asyncio.AbstractEventLoop) -> None:
    """
    시그널 핸들러 설정

    SIGINT (Ctrl+C) 및 SIGTERM 시 graceful shutdown을 수행합니다.
    """
    async def _shutdown():
        logger.info("종료 시그널 수신, 서버 종료 중...")
        await server.stop()

    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(
            sig,
            lambda: asyncio.ensure_future(_shutdown()),
        )


async def serve(config: Optional[AgentConfig] = None) -> None:
    """
    gRPC 보안 서비스 서버 실행

    서버를 시작하고 종료 시그널을 대기합니다.

    Args:
        config: 에이전트 설정. None이면 기본 설정 사용.
    """
    if config is None:
        config = AgentConfig()

    server = _GrpcServer(config)

    # 시그널 핸들러 설정
    loop = asyncio.get_running_loop()
    _setup_signal_handlers(server, loop)

    try:
        await server.start()
        logger.info(
            "보안 에이전트 gRPC 서버 실행 중 "
            "(호스트: %s, 포트: %d)",
            config.server.host, config.server.port,
        )
        await server.wait_for_termination()
    except Exception as e:
        logger.error("서버 실행 오류: %s", e)
        raise
    finally:
        await server.stop()


# ============================================================
# 엔트리포인트
# ============================================================

def main() -> None:
    """CLI 엔트리포인트"""
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )
    asyncio.run(serve())


if __name__ == "__main__":
    main()
