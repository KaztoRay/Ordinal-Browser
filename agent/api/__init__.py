"""
API 모듈
========

gRPC 서버를 통해 C++ 브라우저 코어와 통신합니다.
SecurityService를 제공하며, URL/스크립트/페이지 분석
및 실시간 위협 스트리밍을 지원합니다.
"""

from agent.api.grpc_server import SecurityServicer, serve

__all__ = [
    "SecurityServicer",
    "serve",
]
