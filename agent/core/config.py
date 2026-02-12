"""
에이전트 설정 모듈
==================

Pydantic BaseSettings 기반 에이전트 전역 설정.
환경 변수, .env 파일, 기본값을 지원합니다.
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Optional

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings


class LLMConfig(BaseSettings):
    """LLM 모델 설정"""
    
    # OpenAI API 설정
    api_key: str = Field(
        default="",
        description="OpenAI API 키",
        alias="OPENAI_API_KEY"
    )
    model_name: str = Field(
        default="gpt-4",
        description="사용할 LLM 모델명"
    )
    temperature: float = Field(
        default=0.1,
        ge=0.0,
        le=2.0,
        description="생성 온도 (낮을수록 결정적)"
    )
    max_tokens: int = Field(
        default=2048,
        ge=1,
        le=8192,
        description="최대 생성 토큰 수"
    )
    
    # 로컬 모델 폴백 설정
    use_local_fallback: bool = Field(
        default=True,
        description="OpenAI 실패 시 로컬 HuggingFace 모델 사용"
    )
    local_model_name: str = Field(
        default="microsoft/DialoGPT-medium",
        description="로컬 폴백 모델명"
    )
    
    # 캐싱 설정
    cache_enabled: bool = Field(default=True, description="응답 캐싱 활성화")
    cache_ttl_seconds: int = Field(default=3600, description="캐시 TTL (초)")
    cache_max_size: int = Field(default=1000, description="캐시 최대 항목 수")
    
    # 재시도 설정
    max_retries: int = Field(default=3, description="최대 재시도 횟수")
    retry_base_delay: float = Field(default=1.0, description="재시도 기본 대기 시간 (초)")
    
    model_config = {"env_prefix": "LLM_", "env_file": ".env", "extra": "ignore"}


class ThreatConfig(BaseSettings):
    """위협 탐지 임계값 설정"""
    
    # 피싱 탐지 임계값 (0.0 ~ 1.0)
    phishing_threshold: float = Field(
        default=0.7,
        ge=0.0,
        le=1.0,
        description="피싱 판정 임계값"
    )
    
    # 악성코드 탐지 임계값
    malware_threshold: float = Field(
        default=0.6,
        ge=0.0,
        le=1.0,
        description="악성코드 판정 임계값"
    )
    
    # XSS 탐지 임계값
    xss_threshold: float = Field(
        default=0.5,
        ge=0.0,
        le=1.0,
        description="XSS 판정 임계값"
    )
    
    # 프라이버시 위협 임계값
    privacy_threshold: float = Field(
        default=0.6,
        ge=0.0,
        le=1.0,
        description="프라이버시 위협 판정 임계값"
    )
    
    # 전체 위험 점수 임계값
    overall_danger_threshold: float = Field(
        default=0.75,
        ge=0.0,
        le=1.0,
        description="전체 위험 판정 임계값"
    )
    
    # 안전 점수 최소값
    safe_score_minimum: int = Field(
        default=70,
        ge=0,
        le=100,
        description="안전 판정 최소 점수"
    )
    
    model_config = {"env_prefix": "THREAT_", "env_file": ".env", "extra": "ignore"}


class ExternalAPIConfig(BaseSettings):
    """외부 API 설정"""
    
    # Google Safe Browsing API
    google_safe_browsing_key: str = Field(
        default="",
        description="Google Safe Browsing API 키"
    )
    google_safe_browsing_url: str = Field(
        default="https://safebrowsing.googleapis.com/v4/threatMatches:find",
        description="Safe Browsing API 엔드포인트"
    )
    
    # VirusTotal API
    virustotal_key: str = Field(
        default="",
        description="VirusTotal API 키"
    )
    virustotal_url: str = Field(
        default="https://www.virustotal.com/api/v3",
        description="VirusTotal API 엔드포인트"
    )
    
    # API 요청 제한
    rate_limit_per_minute: int = Field(
        default=30,
        description="분당 최대 API 요청 수"
    )
    request_timeout: float = Field(
        default=10.0,
        description="API 요청 타임아웃 (초)"
    )
    
    model_config = {"env_prefix": "API_", "env_file": ".env", "extra": "ignore"}


class DatabaseConfig(BaseSettings):
    """데이터베이스 설정"""
    
    db_path: str = Field(
        default="data/threats.db",
        description="SQLite DB 파일 경로"
    )
    
    # 차단 목록 경로
    blocklist_path: str = Field(
        default="data/blocklists",
        description="차단 목록 디렉토리 경로"
    )
    
    # 임베딩 캐시 경로
    embedding_cache_path: str = Field(
        default="data/embeddings_cache",
        description="임베딩 벡터 캐시 경로"
    )
    
    model_config = {"env_prefix": "DB_", "env_file": ".env", "extra": "ignore"}


class ServerConfig(BaseSettings):
    """gRPC 서버 설정"""
    
    host: str = Field(default="localhost", description="gRPC 서버 호스트")
    port: int = Field(default=50051, ge=1024, le=65535, description="gRPC 서버 포트")
    max_workers: int = Field(default=4, ge=1, le=32, description="gRPC 워커 스레드 수")
    max_message_size: int = Field(
        default=10 * 1024 * 1024,  # 10MB
        description="최대 gRPC 메시지 크기"
    )
    
    model_config = {"env_prefix": "GRPC_", "env_file": ".env", "extra": "ignore"}


class AgentConfig(BaseSettings):
    """
    에이전트 전역 설정
    
    모든 하위 설정을 통합 관리합니다.
    환경 변수 또는 .env 파일에서 설정을 로드합니다.
    """
    
    # 에이전트 기본 정보
    agent_name: str = Field(default="ordinal-security-agent", description="에이전트 이름")
    version: str = Field(default="0.1.0", description="에이전트 버전")
    debug: bool = Field(default=False, description="디버그 모드")
    log_level: str = Field(default="INFO", description="로그 레벨")
    
    # 데이터 디렉토리
    data_dir: str = Field(default="data", description="데이터 저장 디렉토리")
    
    # 하위 설정
    llm: LLMConfig = Field(default_factory=LLMConfig)
    threats: ThreatConfig = Field(default_factory=ThreatConfig)
    external_api: ExternalAPIConfig = Field(default_factory=ExternalAPIConfig)
    database: DatabaseConfig = Field(default_factory=DatabaseConfig)
    server: ServerConfig = Field(default_factory=ServerConfig)
    
    model_config = {"env_prefix": "AGENT_", "env_file": ".env", "extra": "ignore"}
    
    @field_validator("data_dir")
    @classmethod
    def ensure_data_dir(cls, v: str) -> str:
        """데이터 디렉토리가 존재하지 않으면 생성"""
        path = Path(v)
        path.mkdir(parents=True, exist_ok=True)
        return str(path)
    
    @field_validator("log_level")
    @classmethod
    def validate_log_level(cls, v: str) -> str:
        """유효한 로그 레벨인지 확인"""
        valid_levels = {"DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"}
        upper = v.upper()
        if upper not in valid_levels:
            raise ValueError(f"유효하지 않은 로그 레벨: {v}. 가능한 값: {valid_levels}")
        return upper
    
    def get_db_full_path(self) -> Path:
        """DB 파일의 전체 경로 반환"""
        return Path(self.data_dir) / self.database.db_path
    
    def get_blocklist_full_path(self) -> Path:
        """차단 목록 디렉토리의 전체 경로 반환"""
        return Path(self.data_dir) / self.database.blocklist_path
