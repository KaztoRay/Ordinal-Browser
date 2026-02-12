"""
임베딩 엔진
===========

텍스트 벡터 임베딩 생성 및 유사도 검색을 제공합니다.

주요 기능:
- 비동기 OpenAI text-embedding-ada-002 임베딩
- 로컬 sentence-transformers SentenceTransformer 폴백
- 코사인 유사도 계산 (numpy dot product)
- 알려진 위협 벡터 DB 매칭 (embed → compare)
- LRU 캐시 (maxsize=1000)
- 배치 임베딩 지원
"""

from __future__ import annotations

import asyncio
import hashlib
import logging
import time
from collections import OrderedDict
from typing import Any, Optional, Sequence

import numpy as np

logger = logging.getLogger(__name__)


# ============================================================
# 코사인 유사도 함수
# ============================================================

def cosine_similarity(vec_a: np.ndarray, vec_b: np.ndarray) -> float:
    """
    두 벡터 간의 코사인 유사도 계산 (numpy dot product)

    Args:
        vec_a: 첫 번째 벡터 (1-D numpy 배열)
        vec_b: 두 번째 벡터 (1-D numpy 배열)

    Returns:
        코사인 유사도 (-1.0 ~ 1.0)
    """
    # 영벡터 방지
    norm_a = np.linalg.norm(vec_a)
    norm_b = np.linalg.norm(vec_b)
    if norm_a == 0.0 or norm_b == 0.0:
        return 0.0

    return float(np.dot(vec_a, vec_b) / (norm_a * norm_b))


def cosine_similarity_batch(
    query_vec: np.ndarray, matrix: np.ndarray
) -> np.ndarray:
    """
    하나의 쿼리 벡터와 벡터 행렬 간의 코사인 유사도 배치 계산

    Args:
        query_vec: 쿼리 벡터 (1-D, shape: [dim])
        matrix: 비교 대상 벡터 행렬 (2-D, shape: [N, dim])

    Returns:
        유사도 배열 (shape: [N])
    """
    if matrix.shape[0] == 0:
        return np.array([], dtype=np.float32)

    # 쿼리 벡터 정규화
    query_norm = np.linalg.norm(query_vec)
    if query_norm == 0.0:
        return np.zeros(matrix.shape[0], dtype=np.float32)

    normalized_query = query_vec / query_norm

    # 행렬 각 행 정규화
    norms = np.linalg.norm(matrix, axis=1, keepdims=True)
    # 영벡터 방지 (0으로 나누기 방지)
    norms = np.where(norms == 0, 1.0, norms)
    normalized_matrix = matrix / norms

    # 내적으로 코사인 유사도 일괄 계산
    similarities = normalized_matrix @ normalized_query
    return similarities.astype(np.float32)


# ============================================================
# LRU 캐시 구현
# ============================================================

class _LRUCache:
    """
    스레드 안전하지 않은 간단한 LRU 캐시

    OrderedDict 기반으로 최대 크기를 초과하면 가장 오래된 항목을 제거합니다.
    """

    def __init__(self, maxsize: int = 1000) -> None:
        self.maxsize = maxsize
        self._store: OrderedDict[str, np.ndarray] = OrderedDict()

    def get(self, key: str) -> Optional[np.ndarray]:
        """키로 조회 (히트 시 최신으로 이동)"""
        if key not in self._store:
            return None
        # 최신으로 이동
        self._store.move_to_end(key)
        return self._store[key]

    def put(self, key: str, value: np.ndarray) -> None:
        """항목 저장 (크기 초과 시 가장 오래된 항목 제거)"""
        if key in self._store:
            self._store.move_to_end(key)
            self._store[key] = value
            return

        if len(self._store) >= self.maxsize:
            # 가장 오래된 항목 (first=True) 제거
            self._store.popitem(last=False)

        self._store[key] = value

    def clear(self) -> None:
        """캐시 전체 삭제"""
        self._store.clear()

    @property
    def size(self) -> int:
        return len(self._store)


# ============================================================
# 임베딩 엔진
# ============================================================

class EmbeddingEngine:
    """
    텍스트 임베딩 엔진

    OpenAI text-embedding-ada-002 API를 기본으로 사용하며,
    실패 시 로컬 sentence-transformers로 폴백합니다.

    LRU 캐시, 배치 처리, 위협 벡터 매칭을 지원합니다.
    """

    # OpenAI 임베딩 모델명
    OPENAI_MODEL = "text-embedding-ada-002"
    # OpenAI ada-002 벡터 차원
    OPENAI_DIMENSION = 1536

    # 로컬 sentence-transformers 기본 모델
    LOCAL_MODEL_NAME = "all-MiniLM-L6-v2"
    # MiniLM 벡터 차원
    LOCAL_DIMENSION = 384

    def __init__(
        self,
        api_key: str = "",
        use_local_fallback: bool = True,
        cache_maxsize: int = 1000,
        local_model_name: Optional[str] = None,
    ) -> None:
        """
        임베딩 엔진 초기화

        Args:
            api_key: OpenAI API 키
            use_local_fallback: 로컬 모델 폴백 사용 여부
            cache_maxsize: LRU 캐시 최대 크기
            local_model_name: 로컬 모델명 (None이면 기본값)
        """
        self._api_key = api_key
        self._use_local_fallback = use_local_fallback
        self._local_model_name = local_model_name or self.LOCAL_MODEL_NAME

        # OpenAI 비동기 클라이언트 (지연 초기화)
        self._openai_client: Any = None

        # 로컬 SentenceTransformer 모델 (지연 초기화)
        self._local_model: Any = None
        self._local_model_loaded = False

        # LRU 캐시
        self._cache = _LRUCache(maxsize=cache_maxsize)
        self._cache_lock = asyncio.Lock()

        # 위협 벡터 DB (이름 → 벡터)
        self._threat_vectors: dict[str, np.ndarray] = {}
        self._threat_matrix: Optional[np.ndarray] = None
        self._threat_labels: list[str] = []

        # 현재 활성 차원
        self._active_dimension: int = self.OPENAI_DIMENSION

        logger.info(
            "EmbeddingEngine 초기화 — OpenAI: %s, 로컬 폴백: %s",
            bool(api_key),
            use_local_fallback,
        )

    # ============================
    # 단일 텍스트 임베딩
    # ============================

    async def embed(self, text: str) -> np.ndarray:
        """
        단일 텍스트의 벡터 임베딩 생성

        1. 캐시 확인
        2. OpenAI API 호출
        3. 실패 시 로컬 모델 폴백

        Args:
            text: 임베딩할 텍스트

        Returns:
            임베딩 벡터 (numpy 1-D 배열)

        Raises:
            RuntimeError: 모든 방법 실패 시
        """
        # 캐시 확인
        cache_key = self._make_cache_key(text)
        async with self._cache_lock:
            cached = self._cache.get(cache_key)
        if cached is not None:
            return cached

        # OpenAI 임베딩 시도
        vector = await self._embed_openai(text)

        # 폴백
        if vector is None and self._use_local_fallback:
            logger.debug("OpenAI 임베딩 실패 → 로컬 모델 폴백")
            vector = await self._embed_local(text)

        if vector is None:
            raise RuntimeError(f"임베딩 생성 실패: {text[:50]}...")

        # 캐시 저장
        async with self._cache_lock:
            self._cache.put(cache_key, vector)

        return vector

    # ============================
    # 배치 임베딩
    # ============================

    async def embed_batch(self, texts: Sequence[str]) -> list[np.ndarray]:
        """
        여러 텍스트의 벡터 임베딩을 배치로 생성

        캐시된 항목은 즉시 반환하고, 미캐시 항목만 API 호출합니다.

        Args:
            texts: 임베딩할 텍스트 리스트

        Returns:
            임베딩 벡터 리스트
        """
        results: list[Optional[np.ndarray]] = [None] * len(texts)
        uncached_indices: list[int] = []
        uncached_texts: list[str] = []

        # 캐시에서 먼저 조회
        async with self._cache_lock:
            for i, text in enumerate(texts):
                key = self._make_cache_key(text)
                cached = self._cache.get(key)
                if cached is not None:
                    results[i] = cached
                else:
                    uncached_indices.append(i)
                    uncached_texts.append(text)

        # 미캐시 항목 배치 임베딩
        if uncached_texts:
            vectors = await self._embed_batch_openai(uncached_texts)

            # OpenAI 실패 시 로컬 배치 폴백
            if vectors is None and self._use_local_fallback:
                logger.debug("OpenAI 배치 임베딩 실패 → 로컬 모델 폴백")
                vectors = await self._embed_batch_local(uncached_texts)

            if vectors is not None and len(vectors) == len(uncached_texts):
                async with self._cache_lock:
                    for idx, vec in zip(uncached_indices, vectors):
                        results[idx] = vec
                        # 캐시 저장
                        key = self._make_cache_key(texts[idx])
                        self._cache.put(key, vec)
            else:
                # 배치 실패 시 개별 처리
                for idx, text in zip(uncached_indices, uncached_texts):
                    try:
                        vec = await self.embed(text)
                        results[idx] = vec
                    except RuntimeError:
                        # 영벡터로 대체
                        results[idx] = np.zeros(self._active_dimension, dtype=np.float32)

        # None 항목은 영벡터로 채움
        final_results: list[np.ndarray] = []
        for r in results:
            if r is not None:
                final_results.append(r)
            else:
                final_results.append(np.zeros(self._active_dimension, dtype=np.float32))

        return final_results

    # ============================
    # 위협 벡터 매칭
    # ============================

    async def register_threat_vectors(
        self, threats: dict[str, str]
    ) -> None:
        """
        알려진 위협 텍스트를 벡터화하여 DB에 등록

        Args:
            threats: 위협 라벨 → 위협 설명/URL 딕셔너리
        """
        logger.info("위협 벡터 %d개 등록 시작", len(threats))

        labels = list(threats.keys())
        texts = list(threats.values())

        # 배치 임베딩
        vectors = await self.embed_batch(texts)

        # 벡터 DB 업데이트
        for label, vec in zip(labels, vectors):
            self._threat_vectors[label] = vec

        # 매칭용 행렬 갱신
        self._threat_labels = list(self._threat_vectors.keys())
        self._threat_matrix = np.array(
            [self._threat_vectors[lbl] for lbl in self._threat_labels],
            dtype=np.float32,
        )

        logger.info("위협 벡터 등록 완료: %d개", len(self._threat_vectors))

    async def match_threats(
        self,
        query_text: str,
        top_k: int = 5,
        threshold: float = 0.7,
    ) -> list[dict[str, Any]]:
        """
        쿼리 텍스트와 가장 유사한 알려진 위협 검색

        Args:
            query_text: 검색할 텍스트 (URL, 설명 등)
            top_k: 반환할 최대 결과 수
            threshold: 최소 유사도 임계값

        Returns:
            유사 위협 리스트 [{"label": ..., "similarity": ..., "rank": ...}]
        """
        if self._threat_matrix is None or self._threat_matrix.shape[0] == 0:
            logger.debug("위협 벡터 DB가 비어 있음")
            return []

        # 쿼리 임베딩
        query_vec = await self.embed(query_text)

        # 코사인 유사도 배치 계산
        similarities = cosine_similarity_batch(query_vec, self._threat_matrix)

        # 임계값 이상만 필터링하고 상위 K개 선택
        matches: list[dict[str, Any]] = []
        # 유사도 높은 순 정렬
        sorted_indices = np.argsort(similarities)[::-1]

        for rank, idx in enumerate(sorted_indices[:top_k], start=1):
            sim = float(similarities[idx])
            if sim < threshold:
                break
            matches.append({
                "label": self._threat_labels[idx],
                "similarity": round(sim, 4),
                "rank": rank,
            })

        return matches

    # ============================
    # OpenAI 임베딩
    # ============================

    async def _embed_openai(self, text: str) -> Optional[np.ndarray]:
        """OpenAI text-embedding-ada-002 단일 임베딩"""
        client = self._get_openai_client()
        if client is None:
            return None

        try:
            response = await client.embeddings.create(
                model=self.OPENAI_MODEL,
                input=text,
            )
            vector = response.data[0].embedding
            self._active_dimension = len(vector)
            return np.array(vector, dtype=np.float32)

        except Exception as e:
            logger.warning("OpenAI 임베딩 실패: %s", e)
            return None

    async def _embed_batch_openai(
        self, texts: Sequence[str]
    ) -> Optional[list[np.ndarray]]:
        """OpenAI 배치 임베딩"""
        client = self._get_openai_client()
        if client is None:
            return None

        try:
            # OpenAI는 최대 2048개까지 배치 지원
            all_vectors: list[np.ndarray] = []

            # 2048개씩 청크 분할
            chunk_size = 2048
            for start in range(0, len(texts), chunk_size):
                chunk = texts[start:start + chunk_size]
                response = await client.embeddings.create(
                    model=self.OPENAI_MODEL,
                    input=list(chunk),
                )
                # 인덱스 순서대로 정렬하여 추가
                sorted_data = sorted(response.data, key=lambda d: d.index)
                for item in sorted_data:
                    vec = np.array(item.embedding, dtype=np.float32)
                    all_vectors.append(vec)

            if all_vectors:
                self._active_dimension = len(all_vectors[0])

            return all_vectors

        except Exception as e:
            logger.warning("OpenAI 배치 임베딩 실패: %s", e)
            return None

    def _get_openai_client(self) -> Any:
        """OpenAI 비동기 클라이언트 지연 초기화"""
        if self._openai_client is not None:
            return self._openai_client

        if not self._api_key:
            logger.debug("OpenAI API 키 미설정")
            return None

        try:
            import openai
            self._openai_client = openai.AsyncOpenAI(api_key=self._api_key)
            return self._openai_client
        except ImportError:
            logger.error("openai 패키지 미설치")
            return None

    # ============================
    # 로컬 sentence-transformers 임베딩
    # ============================

    async def _embed_local(self, text: str) -> Optional[np.ndarray]:
        """로컬 SentenceTransformer 단일 임베딩"""
        model = await self._get_local_model()
        if model is None:
            return None

        try:
            loop = asyncio.get_running_loop()
            vector = await loop.run_in_executor(
                None, lambda: model.encode(text, convert_to_numpy=True)
            )
            vec = np.array(vector, dtype=np.float32).flatten()
            self._active_dimension = len(vec)
            return vec

        except Exception as e:
            logger.error("로컬 임베딩 실패: %s", e)
            return None

    async def _embed_batch_local(
        self, texts: Sequence[str]
    ) -> Optional[list[np.ndarray]]:
        """로컬 SentenceTransformer 배치 임베딩"""
        model = await self._get_local_model()
        if model is None:
            return None

        try:
            loop = asyncio.get_running_loop()
            vectors = await loop.run_in_executor(
                None,
                lambda: model.encode(
                    list(texts),
                    convert_to_numpy=True,
                    batch_size=64,
                    show_progress_bar=False,
                ),
            )
            result = [
                np.array(v, dtype=np.float32).flatten()
                for v in vectors
            ]
            if result:
                self._active_dimension = len(result[0])
            return result

        except Exception as e:
            logger.error("로컬 배치 임베딩 실패: %s", e)
            return None

    async def _get_local_model(self) -> Any:
        """로컬 SentenceTransformer 모델 지연 로딩"""
        if self._local_model is not None:
            return self._local_model

        if self._local_model_loaded:
            # 이미 로딩 시도 후 실패
            return None

        try:
            from sentence_transformers import SentenceTransformer

            logger.info("로컬 임베딩 모델 로딩: %s", self._local_model_name)

            loop = asyncio.get_running_loop()
            self._local_model = await loop.run_in_executor(
                None,
                lambda: SentenceTransformer(self._local_model_name),
            )
            self._local_model_loaded = True
            self._active_dimension = self.LOCAL_DIMENSION
            logger.info("로컬 임베딩 모델 로딩 완료")
            return self._local_model

        except ImportError:
            logger.error("sentence-transformers 미설치: pip install sentence-transformers")
            self._local_model_loaded = True
            return None
        except Exception as e:
            logger.error("로컬 임베딩 모델 로딩 실패: %s", e)
            self._local_model_loaded = True
            return None

    # ============================
    # 캐시 관리
    # ============================

    @staticmethod
    def _make_cache_key(text: str) -> str:
        """텍스트 기반 캐시 키 생성 (SHA-256)"""
        return hashlib.sha256(text.encode("utf-8")).hexdigest()

    async def clear_cache(self) -> None:
        """임베딩 캐시 전체 삭제"""
        async with self._cache_lock:
            self._cache.clear()
        logger.info("임베딩 캐시 초기화 완료")

    @property
    def cache_size(self) -> int:
        """현재 캐시 크기"""
        return self._cache.size

    @property
    def threat_db_size(self) -> int:
        """등록된 위협 벡터 수"""
        return len(self._threat_vectors)

    @property
    def dimension(self) -> int:
        """현재 활성 임베딩 차원"""
        return self._active_dimension

    # ============================
    # 매직 메서드
    # ============================

    def __repr__(self) -> str:
        return (
            f"EmbeddingEngine(dimension={self._active_dimension}, "
            f"cache={self._cache.size}, "
            f"threats={self.threat_db_size})"
        )
