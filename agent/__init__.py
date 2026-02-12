"""
Ordinal Browser 보안 에이전트
============================

LLM 기반 실시간 웹 보안 분석 에이전트.
피싱 탐지, 악성코드 분석, 프라이버시 추적기 차단,
취약점 스캔을 수행합니다.

gRPC 서버로 C++ 브라우저 코어와 통신합니다.
"""

__version__ = "0.1.0"
__author__ = "Ordinal Project"
__description__ = "AI 기반 웹 보안 분석 에이전트"

from agent.core.agent import SecurityAgent
from agent.core.config import AgentConfig

__all__ = [
    "SecurityAgent",
    "AgentConfig",
    "__version__",
]
