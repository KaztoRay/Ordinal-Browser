"""
LLM 추론 엔진
==============

OpenAI GPT-4 비동기 클라이언트 및 로컬 HuggingFace 모델 폴백을 제공합니다.

주요 기능:
- 비동기 OpenAI GPT-4 호출 (openai.AsyncOpenAI)
- 시스템/유저 메시지 구성 및 구조화된 JSON 출력 (json_mode)
- 지수 백오프 재시도 (최대 3회, 기본 1초)
- TTL 기반 응답 캐시 (딕셔너리)
- tiktoken 기반 토큰 카운팅
- 로컬 HuggingFace transformers AutoModelForCausalLM 폴백
- temperature / max_tokens 설정
"""

from __future__ import annotations

import asyncio
import hashlib
import json
import logging
import time
from typing import Any, Optional

from agent.core.config import LLMConfig

logger = logging.getLogger(__name__)


# ============================================================
# 캐시 항목 데이터 클래스
# ============================================================

class _CacheEntry:
    """TTL 기반 응답 캐시 항목"""

    __slots__ = ("response", "created_at", "ttl")

    def __init__(self, response: dict[str, Any], created_at: float, ttl: float) -> None:
        self.response = response
        self.created_at = created_at
        self.ttl = ttl

    @property
    def is_expired(self) -> bool:
        """TTL 초과 여부 확인"""
        return (time.time() - self.created_at) > self.ttl


# ============================================================
# LLM 추론 엔진
# ============================================================

class LLMInference:
    """
    LLM 추론 엔진

    OpenAI GPT-4 API를 기본으로 사용하며,
    API 호출 실패 시 로컬 HuggingFace 모델로 폴백합니다.

    응답 캐싱, 토큰 카운팅, 재시도 메커니즘을 제공합니다.
    """

    def __init__(self, config: Optional[LLMConfig] = None) -> None:
        """
        LLM 추론 엔진 초기화

        Args:
            config: LLM 설정. None이면 기본 설정 사용.
        """
        self.config = config or LLMConfig()

        # OpenAI 비동기 클라이언트 (지연 초기화)
        self._openai_client: Any = None

        # 로컬 HuggingFace 모델 (지연 초기화)
        self._local_model: Any = None
        self._local_tokenizer: Any = None
        self._local_model_loaded = False

        # 응답 캐시 (해시 키 → _CacheEntry)
        self._cache: dict[str, _CacheEntry] = {}
        self._cache_lock = asyncio.Lock()

        # tiktoken 인코딩 (지연 초기화)
        self._tiktoken_encoding: Any = None

        logger.info(
            "LLMInference 초기화 — 모델: %s, 폴백: %s",
            self.config.model_name,
            self.config.use_local_fallback,
        )

    # ============================
    # 공개 API
    # ============================

    async def generate(
        self,
        system_prompt: str,
        user_prompt: str,
        temperature: Optional[float] = None,
        max_tokens: Optional[int] = None,
        json_mode: bool = True,
    ) -> dict[str, Any]:
        """
        LLM에 프롬프트를 전송하고 구조화된 JSON 응답을 반환

        1. 캐시 확인
        2. OpenAI API 호출 (재시도 포함)
        3. 실패 시 로컬 모델 폴백
        4. 응답 캐싱

        Args:
            system_prompt: 시스템 프롬프트 (역할/규칙 정의)
            user_prompt: 사용자 프롬프트 (분석 대상)
            temperature: 생성 온도 (None이면 설정값 사용)
            max_tokens: 최대 토큰 수 (None이면 설정값 사용)
            json_mode: JSON 형식 응답 강제 여부

        Returns:
            파싱된 JSON 응답 딕셔너리
        """
        # 사용할 파라미터 결정
        temp = temperature if temperature is not None else self.config.temperature
        tokens = max_tokens if max_tokens is not None else self.config.max_tokens

        # 캐시 키 생성 및 조회
        cache_key = self._make_cache_key(system_prompt, user_prompt, temp, tokens)

        if self.config.cache_enabled:
            cached = await self._get_cached(cache_key)
            if cached is not None:
                logger.debug("캐시 히트: %s", cache_key[:16])
                return cached

        # OpenAI API 호출 시도
        response = await self._call_openai(
            system_prompt=system_prompt,
            user_prompt=user_prompt,
            temperature=temp,
            max_tokens=tokens,
            json_mode=json_mode,
        )

        # OpenAI 실패 시 로컬 폴백
        if response is None and self.config.use_local_fallback:
            logger.warning("OpenAI API 실패 → 로컬 HuggingFace 모델 폴백")
            response = await self._call_local_model(
                system_prompt=system_prompt,
                user_prompt=user_prompt,
                temperature=temp,
                max_tokens=tokens,
            )

        # 응답이 없으면 빈 딕셔너리 반환
        if response is None:
            logger.error("LLM 추론 완전 실패: OpenAI + 로컬 모두 실패")
            return {}

        # 캐시 저장
        if self.config.cache_enabled:
            await self._set_cached(cache_key, response)

        return response

    def count_tokens(self, text: str) -> int:
        """
        tiktoken을 사용하여 텍스트의 토큰 수 계산

        Args:
            text: 토큰 수를 셀 텍스트

        Returns:
            토큰 수
        """
        encoding = self._get_tiktoken_encoding()
        if encoding is not None:
            return len(encoding.encode(text))

        # tiktoken 사용 불가 시 대략적 추정 (4자 ≈ 1토큰)
        return len(text) // 4

    def count_messages_tokens(
        self, system_prompt: str, user_prompt: str
    ) -> int:
        """
        메시지 전체의 토큰 수 계산 (오버헤드 포함)

        GPT-4 메시지 형식의 토큰 오버헤드를 포함합니다.

        Args:
            system_prompt: 시스템 프롬프트
            user_prompt: 사용자 프롬프트

        Returns:
            전체 메시지 토큰 수
        """
        # 메시지 토큰 오버헤드: 각 메시지당 ~4토큰 + 응답 프라이밍 3토큰
        overhead_per_message = 4
        reply_priming = 3

        system_tokens = self.count_tokens(system_prompt) + overhead_per_message
        user_tokens = self.count_tokens(user_prompt) + overhead_per_message

        return system_tokens + user_tokens + reply_priming

    def estimate_cost(
        self, system_prompt: str, user_prompt: str, max_output_tokens: int = 0
    ) -> dict[str, float]:
        """
        API 호출 예상 비용 계산 (USD)

        GPT-4 가격 기준:
        - 입력: $0.03/1K 토큰
        - 출력: $0.06/1K 토큰

        Args:
            system_prompt: 시스템 프롬프트
            user_prompt: 사용자 프롬프트
            max_output_tokens: 예상 출력 토큰 (0이면 설정값 사용)

        Returns:
            비용 정보 딕셔너리 (input_tokens, output_tokens, estimated_cost_usd)
        """
        input_tokens = self.count_messages_tokens(system_prompt, user_prompt)
        output_tokens = max_output_tokens or self.config.max_tokens

        # GPT-4 가격 (2024년 기준)
        input_cost = (input_tokens / 1000) * 0.03
        output_cost = (output_tokens / 1000) * 0.06

        return {
            "input_tokens": input_tokens,
            "output_tokens": output_tokens,
            "estimated_cost_usd": round(input_cost + output_cost, 6),
        }

    async def clear_cache(self) -> None:
        """응답 캐시 전체 삭제"""
        async with self._cache_lock:
            self._cache.clear()
            logger.info("LLM 응답 캐시 초기화 완료")

    @property
    def cache_size(self) -> int:
        """현재 캐시 항목 수"""
        return len(self._cache)

    # ============================
    # OpenAI API 호출
    # ============================

    async def _call_openai(
        self,
        system_prompt: str,
        user_prompt: str,
        temperature: float,
        max_tokens: int,
        json_mode: bool,
    ) -> Optional[dict[str, Any]]:
        """
        OpenAI GPT-4 비동기 호출 (지수 백오프 재시도)

        Args:
            system_prompt: 시스템 메시지
            user_prompt: 사용자 메시지
            temperature: 생성 온도
            max_tokens: 최대 토큰
            json_mode: JSON 응답 모드

        Returns:
            파싱된 JSON 딕셔너리 또는 None (실패)
        """
        # API 키 확인
        if not self.config.api_key:
            logger.warning("OpenAI API 키가 설정되지 않음")
            return None

        # 클라이언트 지연 초기화
        client = self._get_openai_client()
        if client is None:
            return None

        # 메시지 구성
        messages = self._build_messages(system_prompt, user_prompt)

        # 응답 형식 설정 (json_mode)
        response_format: Optional[dict[str, str]] = None
        if json_mode:
            response_format = {"type": "json_object"}

        # 지수 백오프 재시도 루프
        last_error: Optional[Exception] = None
        for attempt in range(self.config.max_retries):
            try:
                # 비동기 ChatCompletion 호출
                completion = await client.chat.completions.create(
                    model=self.config.model_name,
                    messages=messages,
                    temperature=temperature,
                    max_tokens=max_tokens,
                    response_format=response_format,
                )

                # 응답에서 콘텐츠 추출
                content = completion.choices[0].message.content
                if not content:
                    logger.warning("OpenAI 응답 콘텐츠가 비어 있음 (시도 %d)", attempt + 1)
                    continue

                # JSON 파싱
                parsed = self._parse_json_response(content)
                if parsed is not None:
                    logger.debug(
                        "OpenAI 호출 성공 (시도 %d, 모델: %s)",
                        attempt + 1, self.config.model_name,
                    )
                    return parsed

                logger.warning(
                    "JSON 파싱 실패 (시도 %d): %s...",
                    attempt + 1, content[:100],
                )

            except Exception as e:
                last_error = e
                logger.warning(
                    "OpenAI API 오류 (시도 %d/%d): %s",
                    attempt + 1, self.config.max_retries, e,
                )

                # 마지막 시도가 아니면 지수 백오프 대기
                if attempt < self.config.max_retries - 1:
                    delay = self.config.retry_base_delay * (2 ** attempt)
                    logger.debug("재시도 대기: %.1f초", delay)
                    await asyncio.sleep(delay)

        logger.error(
            "OpenAI API 호출 최종 실패 (%d회 시도): %s",
            self.config.max_retries, last_error,
        )
        return None

    def _get_openai_client(self) -> Any:
        """OpenAI 비동기 클라이언트 지연 초기화"""
        if self._openai_client is not None:
            return self._openai_client

        try:
            import openai
            self._openai_client = openai.AsyncOpenAI(api_key=self.config.api_key)
            logger.debug("OpenAI AsyncOpenAI 클라이언트 초기화 완료")
            return self._openai_client
        except ImportError:
            logger.error("openai 패키지가 설치되지 않음: pip install openai")
            return None
        except Exception as e:
            logger.error("OpenAI 클라이언트 초기화 실패: %s", e)
            return None

    def _build_messages(
        self, system_prompt: str, user_prompt: str
    ) -> list[dict[str, str]]:
        """
        OpenAI ChatCompletion 메시지 배열 구성

        Args:
            system_prompt: 시스템 역할 메시지
            user_prompt: 사용자 입력 메시지

        Returns:
            메시지 딕셔너리 리스트
        """
        messages: list[dict[str, str]] = []

        # 시스템 메시지 (역할/규칙 정의)
        if system_prompt:
            messages.append({
                "role": "system",
                "content": system_prompt,
            })

        # 사용자 메시지 (분석 대상)
        messages.append({
            "role": "user",
            "content": user_prompt,
        })

        return messages

    # ============================
    # 로컬 HuggingFace 모델 폴백
    # ============================

    async def _call_local_model(
        self,
        system_prompt: str,
        user_prompt: str,
        temperature: float,
        max_tokens: int,
    ) -> Optional[dict[str, Any]]:
        """
        로컬 HuggingFace transformers 모델 폴백

        AutoModelForCausalLM을 사용하여 로컬에서 추론합니다.
        CPU/GPU 자동 감지하며, 비동기 실행을 위해 executor를 사용합니다.

        Args:
            system_prompt: 시스템 프롬프트
            user_prompt: 사용자 프롬프트
            temperature: 생성 온도
            max_tokens: 최대 토큰 수

        Returns:
            파싱된 JSON 딕셔너리 또는 None
        """
        try:
            # 모델 로딩 (최초 1회)
            if not self._local_model_loaded:
                await self._load_local_model()

            if self._local_model is None or self._local_tokenizer is None:
                logger.error("로컬 모델이 로드되지 않음")
                return None

            # 프롬프트 결합
            combined_prompt = self._format_local_prompt(system_prompt, user_prompt)

            # 블로킹 추론을 executor에서 실행
            loop = asyncio.get_running_loop()
            result = await loop.run_in_executor(
                None,
                self._generate_local,
                combined_prompt,
                temperature,
                max_tokens,
            )

            if result is None:
                return None

            # JSON 파싱 시도
            parsed = self._parse_json_response(result)
            if parsed is not None:
                logger.debug("로컬 모델 JSON 출력 성공")
                return parsed

            # JSON 파싱 실패 시 기본 응답 구조 생성
            logger.warning("로컬 모델 JSON 파싱 실패 → 기본 응답 구조 생성")
            return {
                "threat_level": "LOW",
                "confidence": 0.3,
                "reasoning": result[:500],
                "indicators": [],
                "recommendation": "로컬 모델 분석 결과 — 정확도 제한적",
            }

        except Exception as e:
            logger.error("로컬 모델 추론 실패: %s", e)
            return None

    async def _load_local_model(self) -> None:
        """로컬 HuggingFace 모델 비동기 로딩"""
        try:
            import torch
            from transformers import AutoModelForCausalLM, AutoTokenizer

            model_name = self.config.local_model_name
            logger.info("로컬 모델 로딩 시작: %s", model_name)

            # 디바이스 자동 선택 (MPS > CUDA > CPU)
            if torch.backends.mps.is_available():
                device = "mps"
            elif torch.cuda.is_available():
                device = "cuda"
            else:
                device = "cpu"
            logger.info("추론 디바이스: %s", device)

            # executor에서 블로킹 로딩 실행
            loop = asyncio.get_running_loop()

            def _load() -> tuple:
                tokenizer = AutoTokenizer.from_pretrained(
                    model_name, trust_remote_code=True
                )
                model = AutoModelForCausalLM.from_pretrained(
                    model_name,
                    trust_remote_code=True,
                    torch_dtype=torch.float16 if device != "cpu" else torch.float32,
                    device_map="auto" if device != "cpu" else None,
                )
                if device == "cpu":
                    model = model.to(device)
                return tokenizer, model

            self._local_tokenizer, self._local_model = await loop.run_in_executor(
                None, _load
            )
            self._local_model_loaded = True
            logger.info("로컬 모델 로딩 완료: %s (%s)", model_name, device)

        except ImportError as e:
            logger.error(
                "HuggingFace transformers 또는 torch 미설치: %s", e
            )
            self._local_model_loaded = True  # 재시도 방지
        except Exception as e:
            logger.error("로컬 모델 로딩 실패: %s", e)
            self._local_model_loaded = True  # 재시도 방지

    def _generate_local(
        self, prompt: str, temperature: float, max_tokens: int
    ) -> Optional[str]:
        """
        로컬 모델에서 텍스트 생성 (동기 — executor에서 호출)

        Args:
            prompt: 입력 프롬프트
            temperature: 생성 온도
            max_tokens: 최대 생성 토큰

        Returns:
            생성된 텍스트 또는 None
        """
        try:
            import torch

            # 토크나이즈
            inputs = self._local_tokenizer(
                prompt,
                return_tensors="pt",
                truncation=True,
                max_length=2048,
            )

            # 모델 디바이스로 이동
            device = next(self._local_model.parameters()).device
            inputs = {k: v.to(device) for k, v in inputs.items()}

            # 생성 파라미터
            gen_kwargs: dict[str, Any] = {
                "max_new_tokens": min(max_tokens, 1024),
                "do_sample": temperature > 0.0,
                "pad_token_id": self._local_tokenizer.eos_token_id,
            }
            if temperature > 0.0:
                gen_kwargs["temperature"] = temperature
                gen_kwargs["top_p"] = 0.9

            # 추론 (그래디언트 계산 비활성화)
            with torch.no_grad():
                output_ids = self._local_model.generate(
                    **inputs, **gen_kwargs
                )

            # 입력 토큰 이후의 생성 부분만 디코딩
            input_length = inputs["input_ids"].shape[1]
            generated_ids = output_ids[0][input_length:]
            generated_text = self._local_tokenizer.decode(
                generated_ids, skip_special_tokens=True
            )

            return generated_text.strip() if generated_text else None

        except Exception as e:
            logger.error("로컬 모델 생성 실패: %s", e)
            return None

    def _format_local_prompt(self, system_prompt: str, user_prompt: str) -> str:
        """
        로컬 모델용 프롬프트 포맷팅

        ChatML 형식으로 시스템/사용자 메시지를 결합합니다.
        """
        parts: list[str] = []

        if system_prompt:
            parts.append(f"<|system|>\n{system_prompt}\n</|system|>")

        parts.append(f"<|user|>\n{user_prompt}\n</|user|>")
        parts.append("<|assistant|>")

        return "\n".join(parts)

    # ============================
    # 토큰 카운팅
    # ============================

    def _get_tiktoken_encoding(self) -> Any:
        """tiktoken 인코딩 객체 지연 초기화"""
        if self._tiktoken_encoding is not None:
            return self._tiktoken_encoding

        try:
            import tiktoken

            # 모델별 인코딩 선택
            model = self.config.model_name
            try:
                self._tiktoken_encoding = tiktoken.encoding_for_model(model)
            except KeyError:
                # 알 수 없는 모델이면 cl100k_base 사용 (GPT-4 기본)
                self._tiktoken_encoding = tiktoken.get_encoding("cl100k_base")

            logger.debug("tiktoken 인코딩 초기화: %s", self._tiktoken_encoding.name)
            return self._tiktoken_encoding

        except ImportError:
            logger.warning("tiktoken 미설치 — 토큰 수 추정 모드 사용")
            return None

    # ============================
    # JSON 응답 파싱
    # ============================

    @staticmethod
    def _parse_json_response(content: str) -> Optional[dict[str, Any]]:
        """
        LLM 응답에서 JSON 객체 추출 및 파싱

        순수 JSON, ```json 블록, 혼합 텍스트 내 JSON을 모두 처리합니다.

        Args:
            content: LLM 응답 텍스트

        Returns:
            파싱된 딕셔너리 또는 None
        """
        if not content:
            return None

        content = content.strip()

        # 1차 시도: 직접 JSON 파싱
        try:
            result = json.loads(content)
            if isinstance(result, dict):
                return result
        except json.JSONDecodeError:
            pass

        # 2차 시도: ```json ... ``` 블록 추출
        import re
        json_block = re.search(r'```(?:json)?\s*\n?(.*?)\n?\s*```', content, re.DOTALL)
        if json_block:
            try:
                result = json.loads(json_block.group(1).strip())
                if isinstance(result, dict):
                    return result
            except json.JSONDecodeError:
                pass

        # 3차 시도: 첫 번째 { ... } 블록 추출
        brace_start = content.find("{")
        brace_end = content.rfind("}")
        if brace_start != -1 and brace_end > brace_start:
            try:
                result = json.loads(content[brace_start:brace_end + 1])
                if isinstance(result, dict):
                    return result
            except json.JSONDecodeError:
                pass

        return None

    # ============================
    # 캐시 관리
    # ============================

    def _make_cache_key(
        self, system_prompt: str, user_prompt: str,
        temperature: float, max_tokens: int,
    ) -> str:
        """프롬프트 기반 캐시 키 생성 (SHA-256 해시)"""
        key_data = f"{system_prompt}|{user_prompt}|{temperature}|{max_tokens}"
        return hashlib.sha256(key_data.encode("utf-8")).hexdigest()

    async def _get_cached(self, key: str) -> Optional[dict[str, Any]]:
        """캐시에서 응답 조회 (TTL 만료 확인)"""
        async with self._cache_lock:
            entry = self._cache.get(key)
            if entry is None:
                return None
            if entry.is_expired:
                del self._cache[key]
                return None
            return entry.response

    async def _set_cached(self, key: str, response: dict[str, Any]) -> None:
        """응답을 캐시에 저장 (최대 크기 제한)"""
        async with self._cache_lock:
            # 캐시 크기 제한
            if len(self._cache) >= self.config.cache_max_size:
                # 가장 오래된 항목 제거 (FIFO)
                oldest_key = next(iter(self._cache))
                del self._cache[oldest_key]

            self._cache[key] = _CacheEntry(
                response=response,
                created_at=time.time(),
                ttl=float(self.config.cache_ttl_seconds),
            )

    # ============================
    # 매직 메서드
    # ============================

    def __repr__(self) -> str:
        return (
            f"LLMInference(model={self.config.model_name!r}, "
            f"cache_size={self.cache_size}, "
            f"local_fallback={self.config.use_local_fallback})"
        )
