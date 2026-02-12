"""
위협 데이터베이스
=================

aiosqlite 기반 비동기 위협 정보 저장소.

스키마:
- threats: URL/도메인별 위협 정보 (id, url, domain, hash, level, type, details, created_at, updated_at)
- iocs: 침해 지표 (id, type, value, source, confidence)
- scan_results: 스캔 결과 (id, url, score, report, timestamp)

기능:
- 비동기 CRUD 연산
- 벌크 임포트
- 통계 쿼리
- 컨텍스트 매니저 (자동 연결/종료)
"""

from __future__ import annotations

import json
import logging
import time
from pathlib import Path
from typing import Any, Optional

import aiosqlite

logger = logging.getLogger(__name__)


# ============================================================
# DB 스키마 정의
# ============================================================

_SCHEMA_SQL = """
-- 위협 정보 테이블
CREATE TABLE IF NOT EXISTS threats (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    url TEXT NOT NULL,
    domain TEXT NOT NULL DEFAULT '',
    hash TEXT NOT NULL DEFAULT '',
    level INTEGER NOT NULL DEFAULT 0,
    type TEXT NOT NULL DEFAULT 'unknown',
    details TEXT NOT NULL DEFAULT '{}',
    created_at REAL NOT NULL,
    updated_at REAL NOT NULL
);

-- URL 인덱스 (빠른 조회)
CREATE INDEX IF NOT EXISTS idx_threats_url ON threats(url);
-- 도메인 인덱스
CREATE INDEX IF NOT EXISTS idx_threats_domain ON threats(domain);
-- 해시 인덱스
CREATE INDEX IF NOT EXISTS idx_threats_hash ON threats(hash);
-- 위협 수준 인덱스
CREATE INDEX IF NOT EXISTS idx_threats_level ON threats(level);

-- 침해 지표 (IoC) 테이블
CREATE TABLE IF NOT EXISTS iocs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    type TEXT NOT NULL,
    value TEXT NOT NULL,
    source TEXT NOT NULL DEFAULT '',
    confidence REAL NOT NULL DEFAULT 0.0
);

-- IoC 타입+값 인덱스
CREATE INDEX IF NOT EXISTS idx_iocs_type_value ON iocs(type, value);

-- 스캔 결과 테이블
CREATE TABLE IF NOT EXISTS scan_results (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    url TEXT NOT NULL,
    score REAL NOT NULL DEFAULT 0.0,
    report TEXT NOT NULL DEFAULT '{}',
    timestamp REAL NOT NULL
);

-- 스캔 결과 URL 인덱스
CREATE INDEX IF NOT EXISTS idx_scan_results_url ON scan_results(url);
-- 스캔 결과 타임스탬프 인덱스
CREATE INDEX IF NOT EXISTS idx_scan_results_timestamp ON scan_results(timestamp);
"""


# ============================================================
# 위협 데이터베이스
# ============================================================

class ThreatDatabase:
    """
    비동기 위협 정보 데이터베이스

    aiosqlite를 사용하여 비동기 SQLite 연산을 수행합니다.
    컨텍스트 매니저 패턴을 지원합니다.

    사용 예:
        async with ThreatDatabase("threats.db") as db:
            await db.add_threat(url="http://evil.com", ...)
            threat = await db.get_threat_by_url("http://evil.com")
    """

    def __init__(self, db_path: str = "data/threats.db") -> None:
        """
        위협 DB 초기화

        Args:
            db_path: SQLite 데이터베이스 파일 경로
        """
        self._db_path = db_path
        self._connection: Optional[aiosqlite.Connection] = None
        self._initialized = False
        logger.info("ThreatDatabase 인스턴스 생성: %s", db_path)

    # ============================
    # 컨텍스트 매니저
    # ============================

    async def __aenter__(self) -> "ThreatDatabase":
        """비동기 컨텍스트 매니저 — DB 연결 및 스키마 초기화"""
        await self.connect()
        return self

    async def __aexit__(self, *args: Any) -> None:
        """비동기 컨텍스트 매니저 — DB 연결 종료"""
        await self.close()

    # ============================
    # 연결 관리
    # ============================

    async def connect(self) -> None:
        """
        DB 연결 및 스키마 초기화

        디렉토리가 없으면 자동 생성합니다.
        """
        if self._connection is not None:
            return

        # 디렉토리 생성
        db_dir = Path(self._db_path).parent
        db_dir.mkdir(parents=True, exist_ok=True)

        # aiosqlite 연결
        self._connection = await aiosqlite.connect(self._db_path)
        # WAL 모드 (동시 읽기 성능 향상)
        await self._connection.execute("PRAGMA journal_mode=WAL")
        # 외래 키 제약 활성화
        await self._connection.execute("PRAGMA foreign_keys=ON")
        # Row 팩토리 설정 (딕셔너리 형태)
        self._connection.row_factory = aiosqlite.Row

        # 스키마 초기화
        await self._connection.executescript(_SCHEMA_SQL)
        await self._connection.commit()

        self._initialized = True
        logger.info("ThreatDatabase 연결 완료: %s", self._db_path)

    async def close(self) -> None:
        """DB 연결 종료"""
        if self._connection is not None:
            await self._connection.close()
            self._connection = None
            self._initialized = False
            logger.info("ThreatDatabase 연결 종료")

    def _ensure_connected(self) -> None:
        """연결 상태 확인"""
        if self._connection is None or not self._initialized:
            raise RuntimeError(
                "ThreatDatabase가 연결되지 않았습니다. "
                "await db.connect() 또는 async with를 사용하세요."
            )

    # ============================
    # Threats CRUD
    # ============================

    async def add_threat(
        self,
        url: str,
        domain: str = "",
        hash_value: str = "",
        level: int = 0,
        threat_type: str = "unknown",
        details: Optional[dict[str, Any]] = None,
    ) -> int:
        """
        위협 정보 추가

        Args:
            url: 위협 URL
            domain: 도메인
            hash_value: 콘텐츠 해시
            level: 위협 수준 (0=SAFE ~ 4=CRITICAL)
            threat_type: 위협 유형 (phishing, malware, xss, privacy)
            details: 상세 정보 딕셔너리

        Returns:
            생성된 레코드 ID
        """
        self._ensure_connected()

        now = time.time()
        details_json = json.dumps(details or {}, ensure_ascii=False)

        cursor = await self._connection.execute(
            """
            INSERT INTO threats (url, domain, hash, level, type, details, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (url, domain, hash_value, level, threat_type, details_json, now, now),
        )
        await self._connection.commit()

        record_id = cursor.lastrowid
        logger.debug("위협 추가: id=%d, url=%s, type=%s", record_id, url, threat_type)
        return record_id

    async def get_threat_by_id(self, threat_id: int) -> Optional[dict[str, Any]]:
        """
        ID로 위협 조회

        Args:
            threat_id: 위협 레코드 ID

        Returns:
            위협 정보 딕셔너리 또는 None
        """
        self._ensure_connected()

        cursor = await self._connection.execute(
            "SELECT * FROM threats WHERE id = ?", (threat_id,)
        )
        row = await cursor.fetchone()
        return self._row_to_dict(row) if row else None

    async def get_threat_by_url(self, url: str) -> Optional[dict[str, Any]]:
        """
        URL로 위협 조회 (가장 최근 항목)

        Args:
            url: 검색할 URL

        Returns:
            위협 정보 딕셔너리 또는 None
        """
        self._ensure_connected()

        cursor = await self._connection.execute(
            "SELECT * FROM threats WHERE url = ? ORDER BY updated_at DESC LIMIT 1",
            (url,),
        )
        row = await cursor.fetchone()
        return self._row_to_dict(row) if row else None

    async def get_threats_by_domain(
        self, domain: str, limit: int = 100
    ) -> list[dict[str, Any]]:
        """
        도메인으로 위협 목록 조회

        Args:
            domain: 검색할 도메인
            limit: 최대 결과 수

        Returns:
            위협 정보 리스트
        """
        self._ensure_connected()

        cursor = await self._connection.execute(
            "SELECT * FROM threats WHERE domain = ? ORDER BY updated_at DESC LIMIT ?",
            (domain, limit),
        )
        rows = await cursor.fetchall()
        return [self._row_to_dict(row) for row in rows]

    async def get_threats_by_level(
        self, min_level: int = 0, limit: int = 100
    ) -> list[dict[str, Any]]:
        """
        위협 수준 이상의 위협 목록 조회

        Args:
            min_level: 최소 위협 수준 (0~4)
            limit: 최대 결과 수

        Returns:
            위협 정보 리스트
        """
        self._ensure_connected()

        cursor = await self._connection.execute(
            "SELECT * FROM threats WHERE level >= ? ORDER BY level DESC, updated_at DESC LIMIT ?",
            (min_level, limit),
        )
        rows = await cursor.fetchall()
        return [self._row_to_dict(row) for row in rows]

    async def update_threat(
        self,
        threat_id: int,
        level: Optional[int] = None,
        threat_type: Optional[str] = None,
        details: Optional[dict[str, Any]] = None,
    ) -> bool:
        """
        위협 정보 업데이트

        Args:
            threat_id: 업데이트할 레코드 ID
            level: 새 위협 수준 (None이면 변경 없음)
            threat_type: 새 위협 유형 (None이면 변경 없음)
            details: 새 상세 정보 (None이면 변경 없음)

        Returns:
            업데이트 성공 여부
        """
        self._ensure_connected()

        # 동적 UPDATE 쿼리 구성
        updates: list[str] = []
        values: list[Any] = []

        if level is not None:
            updates.append("level = ?")
            values.append(level)
        if threat_type is not None:
            updates.append("type = ?")
            values.append(threat_type)
        if details is not None:
            updates.append("details = ?")
            values.append(json.dumps(details, ensure_ascii=False))

        if not updates:
            return False

        updates.append("updated_at = ?")
        values.append(time.time())
        values.append(threat_id)

        query = f"UPDATE threats SET {', '.join(updates)} WHERE id = ?"
        cursor = await self._connection.execute(query, values)
        await self._connection.commit()

        updated = cursor.rowcount > 0
        if updated:
            logger.debug("위협 업데이트: id=%d", threat_id)
        return updated

    async def delete_threat(self, threat_id: int) -> bool:
        """
        위협 정보 삭제

        Args:
            threat_id: 삭제할 레코드 ID

        Returns:
            삭제 성공 여부
        """
        self._ensure_connected()

        cursor = await self._connection.execute(
            "DELETE FROM threats WHERE id = ?", (threat_id,)
        )
        await self._connection.commit()

        deleted = cursor.rowcount > 0
        if deleted:
            logger.debug("위협 삭제: id=%d", threat_id)
        return deleted

    # ============================
    # IoCs CRUD
    # ============================

    async def add_ioc(
        self,
        ioc_type: str,
        value: str,
        source: str = "",
        confidence: float = 0.0,
    ) -> int:
        """
        침해 지표 (IoC) 추가

        Args:
            ioc_type: 지표 유형 (ip, domain, hash, url, email)
            value: 지표 값
            source: 출처
            confidence: 신뢰도 (0.0~1.0)

        Returns:
            생성된 레코드 ID
        """
        self._ensure_connected()

        cursor = await self._connection.execute(
            "INSERT INTO iocs (type, value, source, confidence) VALUES (?, ?, ?, ?)",
            (ioc_type, value, source, confidence),
        )
        await self._connection.commit()
        return cursor.lastrowid

    async def get_ioc(
        self, ioc_type: str, value: str
    ) -> Optional[dict[str, Any]]:
        """
        IoC 타입+값으로 조회

        Args:
            ioc_type: 지표 유형
            value: 지표 값

        Returns:
            IoC 딕셔너리 또는 None
        """
        self._ensure_connected()

        cursor = await self._connection.execute(
            "SELECT * FROM iocs WHERE type = ? AND value = ? LIMIT 1",
            (ioc_type, value),
        )
        row = await cursor.fetchone()
        return self._row_to_dict(row) if row else None

    async def get_iocs_by_type(
        self, ioc_type: str, limit: int = 100
    ) -> list[dict[str, Any]]:
        """
        타입별 IoC 목록 조회

        Args:
            ioc_type: 지표 유형
            limit: 최대 결과 수

        Returns:
            IoC 리스트
        """
        self._ensure_connected()

        cursor = await self._connection.execute(
            "SELECT * FROM iocs WHERE type = ? ORDER BY confidence DESC LIMIT ?",
            (ioc_type, limit),
        )
        rows = await cursor.fetchall()
        return [self._row_to_dict(row) for row in rows]

    async def delete_ioc(self, ioc_id: int) -> bool:
        """IoC 삭제"""
        self._ensure_connected()

        cursor = await self._connection.execute(
            "DELETE FROM iocs WHERE id = ?", (ioc_id,)
        )
        await self._connection.commit()
        return cursor.rowcount > 0

    # ============================
    # Scan Results CRUD
    # ============================

    async def add_scan_result(
        self,
        url: str,
        score: float,
        report: Optional[dict[str, Any]] = None,
    ) -> int:
        """
        스캔 결과 저장

        Args:
            url: 스캔된 URL
            score: 위험 점수 (0.0~1.0)
            report: 상세 보고서 딕셔너리

        Returns:
            생성된 레코드 ID
        """
        self._ensure_connected()

        report_json = json.dumps(report or {}, ensure_ascii=False)
        now = time.time()

        cursor = await self._connection.execute(
            "INSERT INTO scan_results (url, score, report, timestamp) VALUES (?, ?, ?, ?)",
            (url, score, report_json, now),
        )
        await self._connection.commit()
        return cursor.lastrowid

    async def get_scan_results_by_url(
        self, url: str, limit: int = 10
    ) -> list[dict[str, Any]]:
        """
        URL별 스캔 결과 조회

        Args:
            url: 검색할 URL
            limit: 최대 결과 수

        Returns:
            스캔 결과 리스트
        """
        self._ensure_connected()

        cursor = await self._connection.execute(
            "SELECT * FROM scan_results WHERE url = ? ORDER BY timestamp DESC LIMIT ?",
            (url, limit),
        )
        rows = await cursor.fetchall()
        return [self._row_to_dict(row) for row in rows]

    async def get_recent_scans(
        self, limit: int = 50
    ) -> list[dict[str, Any]]:
        """
        최근 스캔 결과 조회

        Args:
            limit: 최대 결과 수

        Returns:
            스캔 결과 리스트
        """
        self._ensure_connected()

        cursor = await self._connection.execute(
            "SELECT * FROM scan_results ORDER BY timestamp DESC LIMIT ?",
            (limit,),
        )
        rows = await cursor.fetchall()
        return [self._row_to_dict(row) for row in rows]

    # ============================
    # 벌크 임포트
    # ============================

    async def bulk_import_threats(
        self, threats: list[dict[str, Any]]
    ) -> int:
        """
        위협 정보 벌크 임포트

        Args:
            threats: 위협 정보 딕셔너리 리스트.
                     각 항목: {"url": ..., "domain": ..., "level": ..., "type": ..., "details": ...}

        Returns:
            임포트된 레코드 수
        """
        self._ensure_connected()

        now = time.time()
        rows = []
        for t in threats:
            details = json.dumps(t.get("details", {}), ensure_ascii=False)
            rows.append((
                t.get("url", ""),
                t.get("domain", ""),
                t.get("hash", ""),
                t.get("level", 0),
                t.get("type", "unknown"),
                details,
                now,
                now,
            ))

        await self._connection.executemany(
            """
            INSERT INTO threats (url, domain, hash, level, type, details, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            rows,
        )
        await self._connection.commit()

        count = len(rows)
        logger.info("벌크 임포트 완료: %d개 위협", count)
        return count

    async def bulk_import_iocs(
        self, iocs: list[dict[str, Any]]
    ) -> int:
        """
        IoC 벌크 임포트

        Args:
            iocs: IoC 딕셔너리 리스트.
                  각 항목: {"type": ..., "value": ..., "source": ..., "confidence": ...}

        Returns:
            임포트된 레코드 수
        """
        self._ensure_connected()

        rows = [
            (
                ioc.get("type", ""),
                ioc.get("value", ""),
                ioc.get("source", ""),
                ioc.get("confidence", 0.0),
            )
            for ioc in iocs
        ]

        await self._connection.executemany(
            "INSERT INTO iocs (type, value, source, confidence) VALUES (?, ?, ?, ?)",
            rows,
        )
        await self._connection.commit()

        count = len(rows)
        logger.info("벌크 IoC 임포트 완료: %d개", count)
        return count

    # ============================
    # 통계 쿼리
    # ============================

    async def get_stats(self) -> dict[str, Any]:
        """
        데이터베이스 통계 조회

        Returns:
            통계 딕셔너리:
            {
                "total_threats": int,
                "threats_by_level": {level: count},
                "threats_by_type": {type: count},
                "total_iocs": int,
                "iocs_by_type": {type: count},
                "total_scans": int,
                "avg_scan_score": float,
                "recent_scan_count_24h": int,
            }
        """
        self._ensure_connected()

        stats: dict[str, Any] = {}

        # 위협 총 수
        cursor = await self._connection.execute("SELECT COUNT(*) FROM threats")
        row = await cursor.fetchone()
        stats["total_threats"] = row[0]

        # 수준별 위협 수
        cursor = await self._connection.execute(
            "SELECT level, COUNT(*) FROM threats GROUP BY level ORDER BY level"
        )
        rows = await cursor.fetchall()
        stats["threats_by_level"] = {row[0]: row[1] for row in rows}

        # 유형별 위협 수
        cursor = await self._connection.execute(
            "SELECT type, COUNT(*) FROM threats GROUP BY type ORDER BY COUNT(*) DESC"
        )
        rows = await cursor.fetchall()
        stats["threats_by_type"] = {row[0]: row[1] for row in rows}

        # IoC 총 수
        cursor = await self._connection.execute("SELECT COUNT(*) FROM iocs")
        row = await cursor.fetchone()
        stats["total_iocs"] = row[0]

        # 유형별 IoC 수
        cursor = await self._connection.execute(
            "SELECT type, COUNT(*) FROM iocs GROUP BY type ORDER BY COUNT(*) DESC"
        )
        rows = await cursor.fetchall()
        stats["iocs_by_type"] = {row[0]: row[1] for row in rows}

        # 스캔 총 수
        cursor = await self._connection.execute("SELECT COUNT(*) FROM scan_results")
        row = await cursor.fetchone()
        stats["total_scans"] = row[0]

        # 평균 스캔 점수
        cursor = await self._connection.execute(
            "SELECT AVG(score) FROM scan_results"
        )
        row = await cursor.fetchone()
        stats["avg_scan_score"] = round(row[0] or 0.0, 4)

        # 최근 24시간 스캔 수
        cutoff = time.time() - 86400
        cursor = await self._connection.execute(
            "SELECT COUNT(*) FROM scan_results WHERE timestamp > ?",
            (cutoff,),
        )
        row = await cursor.fetchone()
        stats["recent_scan_count_24h"] = row[0]

        return stats

    # ============================
    # 검색
    # ============================

    async def search_threats(
        self,
        query: str,
        limit: int = 50,
    ) -> list[dict[str, Any]]:
        """
        위협 정보 텍스트 검색 (URL, 도메인, 상세정보)

        Args:
            query: 검색 키워드
            limit: 최대 결과 수

        Returns:
            매칭된 위협 리스트
        """
        self._ensure_connected()

        like_query = f"%{query}%"
        cursor = await self._connection.execute(
            """
            SELECT * FROM threats
            WHERE url LIKE ? OR domain LIKE ? OR details LIKE ?
            ORDER BY updated_at DESC LIMIT ?
            """,
            (like_query, like_query, like_query, limit),
        )
        rows = await cursor.fetchall()
        return [self._row_to_dict(row) for row in rows]

    # ============================
    # 유틸리티
    # ============================

    @staticmethod
    def _row_to_dict(row: Any) -> dict[str, Any]:
        """
        aiosqlite.Row를 딕셔너리로 변환

        JSON 필드(details, report)는 자동 파싱합니다.
        """
        if row is None:
            return {}

        d = dict(row)

        # JSON 필드 자동 파싱
        for json_field in ("details", "report"):
            if json_field in d and isinstance(d[json_field], str):
                try:
                    d[json_field] = json.loads(d[json_field])
                except (json.JSONDecodeError, TypeError):
                    pass

        return d

    @property
    def is_connected(self) -> bool:
        """연결 상태"""
        return self._connection is not None and self._initialized

    def __repr__(self) -> str:
        return (
            f"ThreatDatabase(path={self._db_path!r}, "
            f"connected={self.is_connected})"
        )
