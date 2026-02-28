"""
보안 보고서 생성기 — HTML/JSON 형식 페이지/세션 보안 보고서

개별 페이지 보안 보고서 및 세션 전체 요약 보고서 생성.
내장 CSS 스타일, 위협 테이블, SVG 점수 게이지 포함.

© 2026 KaztoRay — MIT License
"""

import json
import time
import logging
from datetime import datetime
from typing import Optional

logger = logging.getLogger(__name__)


# ============================================================
# 보고서 생성기
# ============================================================

class ReportGenerator:
    """
    보안 분석 결과를 HTML/JSON 보고서로 생성.
    
    기능:
      - 개별 페이지 보안 보고서 (위협 테이블, 점수 게이지, 권장 사항)
      - 세션 전체 요약 보고서 (집계 통계, 상위 위협)
      - JSON 내보내기
    
    사용법:
        gen = ReportGenerator()
        html = gen.generate_page_report(
            url="https://example.com",
            threats=[{"type": "XSS", "severity": "high", ...}],
            score=72
        )
    """

    # Catppuccin Mocha 기반 보고서 색상
    COLORS = {
        "bg":       "#1e1e2e",
        "surface":  "#313244",
        "overlay":  "#45475a",
        "text":     "#cdd6f4",
        "subtext":  "#a6adc8",
        "accent":   "#89b4fa",
        "green":    "#a6e3a1",
        "yellow":   "#f9e2af",
        "red":      "#f38ba8",
        "peach":    "#fab387",
        "mauve":    "#cba6f7",
    }

    # 심각도별 색상 매핑
    SEVERITY_COLORS = {
        "critical": "#f38ba8",  # 빨강
        "high":     "#fab387",  # 주황
        "medium":   "#f9e2af",  # 노랑
        "low":      "#a6e3a1",  # 초록
        "info":     "#89b4fa",  # 파랑
    }

    def __init__(self):
        logger.info("[ReportGenerator] 초기화")

    # ============================================================
    # 개별 페이지 보안 보고서
    # ============================================================

    def generate_page_report(
        self,
        url: str,
        threats: list[dict],
        score: int,
        recommendations: Optional[list[str]] = None,
    ) -> str:
        """
        개별 페이지 보안 보고서 HTML 생성.
        
        Args:
            url: 분석 대상 URL
            threats: 위협 목록 [{"type": str, "severity": str, 
                     "description": str, "source": str}, ...]
            score: 보안 점수 (0~100)
            recommendations: 권장 사항 목록
            
        Returns:
            str: 완전한 HTML 보고서 문자열
        """
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        threat_rows = "\n".join(self._render_threat_row(t) for t in threats)
        score_gauge = self._render_score_gauge(score)
        rec_list = recommendations or []

        # 권장 사항 HTML
        rec_html = ""
        if rec_list:
            rec_items = "\n".join(
                f'<li class="rec-item">{r}</li>' for r in rec_list
            )
            rec_html = f"""
            <div class="section">
                <h2>💡 권장 사항</h2>
                <ul class="rec-list">
                    {rec_items}
                </ul>
            </div>
            """

        # 위협 통계
        total_threats = len(threats)
        critical_count = sum(1 for t in threats if t.get("severity") == "critical")
        high_count = sum(1 for t in threats if t.get("severity") == "high")
        medium_count = sum(1 for t in threats if t.get("severity") == "medium")
        low_count = sum(1 for t in threats if t.get("severity") == "low")

        html = f"""<!DOCTYPE html>
<html lang="ko">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>보안 보고서 — {self._escape_html(url[:60])}</title>
    <style>
        {self._embedded_css()}
    </style>
</head>
<body>
    <div class="container">
        <!-- 헤더 -->
        <div class="header">
            <h1>🛡️ OrdinalV8 — 보안 보고서</h1>
            <p class="subtitle">생성 시각: {now}</p>
        </div>

        <!-- URL 정보 -->
        <div class="section url-section">
            <h2>📄 분석 대상</h2>
            <div class="url-box">
                <code>{self._escape_html(url)}</code>
            </div>
        </div>

        <!-- 점수 게이지 -->
        <div class="section score-section">
            <h2>📊 보안 점수</h2>
            <div class="score-container">
                {score_gauge}
                <div class="score-details">
                    <span class="score-number">{score}</span>
                    <span class="score-label">/ 100</span>
                </div>
            </div>
        </div>

        <!-- 위협 요약 -->
        <div class="section">
            <h2>⚠️ 위협 요약</h2>
            <div class="stats-row">
                <div class="stat-card" style="border-left: 4px solid {self.SEVERITY_COLORS['critical']};">
                    <span class="stat-num">{critical_count}</span>
                    <span class="stat-label">Critical</span>
                </div>
                <div class="stat-card" style="border-left: 4px solid {self.SEVERITY_COLORS['high']};">
                    <span class="stat-num">{high_count}</span>
                    <span class="stat-label">High</span>
                </div>
                <div class="stat-card" style="border-left: 4px solid {self.SEVERITY_COLORS['medium']};">
                    <span class="stat-num">{medium_count}</span>
                    <span class="stat-label">Medium</span>
                </div>
                <div class="stat-card" style="border-left: 4px solid {self.SEVERITY_COLORS['low']};">
                    <span class="stat-num">{low_count}</span>
                    <span class="stat-label">Low</span>
                </div>
            </div>
        </div>

        <!-- 위협 상세 테이블 -->
        <div class="section">
            <h2>🔍 위협 상세 ({total_threats}건)</h2>
            <table class="threat-table">
                <thead>
                    <tr>
                        <th>심각도</th>
                        <th>유형</th>
                        <th>설명</th>
                        <th>출처</th>
                    </tr>
                </thead>
                <tbody>
                    {threat_rows if threat_rows else '<tr><td colspan="4" class="no-data">감지된 위협 없음 ✅</td></tr>'}
                </tbody>
            </table>
        </div>

        {rec_html}

        <!-- 푸터 -->
        <div class="footer">
            <p>OrdinalV8 v2.0.0 — LLM Security Agent</p>
            <p>© 2026 KaztoRay — MIT License</p>
        </div>
    </div>
</body>
</html>"""

        logger.info("[ReportGenerator] 페이지 보고서 생성: %s (점수: %d, 위협: %d건)",
                    url[:60], score, total_threats)
        return html

    # ============================================================
    # 세션 전체 요약 보고서
    # ============================================================

    def generate_session_report(self, pages: list[dict]) -> str:
        """
        세션 전체 보안 요약 보고서 HTML 생성.
        
        Args:
            pages: 페이지별 분석 결과 목록
                   [{"url": str, "score": int, "threats": list, 
                     "analyzed_at": str}, ...]
            
        Returns:
            str: 세션 요약 HTML 보고서
        """
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        total_pages = len(pages)

        # 집계 통계
        scores = [p.get("score", 0) for p in pages]
        avg_score = sum(scores) / max(len(scores), 1)
        min_score = min(scores) if scores else 0
        max_score = max(scores) if scores else 0

        all_threats = []
        for page in pages:
            for threat in page.get("threats", []):
                threat["page_url"] = page.get("url", "")
                all_threats.append(threat)

        total_threats = len(all_threats)

        # 심각도별 집계
        severity_counts = {"critical": 0, "high": 0, "medium": 0, "low": 0, "info": 0}
        for t in all_threats:
            sev = t.get("severity", "info")
            severity_counts[sev] = severity_counts.get(sev, 0) + 1

        # 위협 유형별 상위 5개
        type_counts: dict[str, int] = {}
        for t in all_threats:
            ttype = t.get("type", "unknown")
            type_counts[ttype] = type_counts.get(ttype, 0) + 1
        top_threats = sorted(type_counts.items(), key=lambda x: x[1], reverse=True)[:5]

        # 상위 위협 HTML
        top_rows = ""
        for ttype, count in top_threats:
            top_rows += f"""
            <tr>
                <td>{self._escape_html(ttype)}</td>
                <td>{count}</td>
            </tr>"""

        # 페이지별 요약 행
        page_rows = ""
        for page in sorted(pages, key=lambda p: p.get("score", 0)):
            url = page.get("url", "")
            score = page.get("score", 0)
            t_count = len(page.get("threats", []))
            score_color = self._score_color(score)
            page_rows += f"""
            <tr>
                <td><code>{self._escape_html(url[:80])}</code></td>
                <td style="color: {score_color}; font-weight: bold;">{score}</td>
                <td>{t_count}</td>
            </tr>"""

        score_gauge = self._render_score_gauge(int(avg_score))

        html = f"""<!DOCTYPE html>
<html lang="ko">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>세션 보안 보고서 — OrdinalV8</title>
    <style>
        {self._embedded_css()}
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>📋 OrdinalV8 — 세션 보안 보고서</h1>
            <p class="subtitle">생성 시각: {now} | 분석 페이지: {total_pages}개</p>
        </div>

        <!-- 전체 통계 -->
        <div class="section score-section">
            <h2>📊 전체 보안 점수 (평균)</h2>
            <div class="score-container">
                {score_gauge}
                <div class="score-details">
                    <span class="score-number">{int(avg_score)}</span>
                    <span class="score-label">/ 100</span>
                    <br>
                    <span class="score-range">최저 {min_score} — 최고 {max_score}</span>
                </div>
            </div>
        </div>

        <!-- 위협 요약 -->
        <div class="section">
            <h2>⚠️ 위협 집계 (총 {total_threats}건)</h2>
            <div class="stats-row">
                <div class="stat-card" style="border-left: 4px solid {self.SEVERITY_COLORS['critical']};">
                    <span class="stat-num">{severity_counts['critical']}</span>
                    <span class="stat-label">Critical</span>
                </div>
                <div class="stat-card" style="border-left: 4px solid {self.SEVERITY_COLORS['high']};">
                    <span class="stat-num">{severity_counts['high']}</span>
                    <span class="stat-label">High</span>
                </div>
                <div class="stat-card" style="border-left: 4px solid {self.SEVERITY_COLORS['medium']};">
                    <span class="stat-num">{severity_counts['medium']}</span>
                    <span class="stat-label">Medium</span>
                </div>
                <div class="stat-card" style="border-left: 4px solid {self.SEVERITY_COLORS['low']};">
                    <span class="stat-num">{severity_counts['low']}</span>
                    <span class="stat-label">Low</span>
                </div>
            </div>
        </div>

        <!-- 상위 위협 유형 -->
        <div class="section">
            <h2>🏆 상위 위협 유형</h2>
            <table class="threat-table">
                <thead>
                    <tr><th>위협 유형</th><th>발생 횟수</th></tr>
                </thead>
                <tbody>
                    {top_rows if top_rows else '<tr><td colspan="2" class="no-data">감지된 위협 없음 ✅</td></tr>'}
                </tbody>
            </table>
        </div>

        <!-- 페이지별 상세 -->
        <div class="section">
            <h2>📄 페이지별 분석 결과</h2>
            <table class="threat-table">
                <thead>
                    <tr><th>URL</th><th>점수</th><th>위협 수</th></tr>
                </thead>
                <tbody>
                    {page_rows}
                </tbody>
            </table>
        </div>

        <div class="footer">
            <p>OrdinalV8 v2.0.0 — LLM Security Agent</p>
            <p>© 2026 KaztoRay — MIT License</p>
        </div>
    </div>
</body>
</html>"""

        logger.info("[ReportGenerator] 세션 보고서 생성: %d페이지, 평균점수 %.1f",
                    total_pages, avg_score)
        return html

    # ============================================================
    # JSON 내보내기
    # ============================================================

    def export_json(self, report_data: dict) -> dict:
        """
        보고서 데이터를 JSON 직렬화 가능한 dict로 변환.
        
        Args:
            report_data: 보고서 원본 데이터
            
        Returns:
            dict: JSON 직렬화 가능한 보고서
        """
        output = {
            "generator": "OrdinalV8 v2.0.0",
            "generated_at": datetime.now().isoformat(),
            "data": report_data,
        }

        # 직렬화 가능한지 확인
        try:
            json.dumps(output, ensure_ascii=False, default=str)
        except (TypeError, ValueError) as e:
            logger.error("[ReportGenerator] JSON 직렬화 오류: %s", str(e))
            output["data"] = str(report_data)

        logger.debug("[ReportGenerator] JSON 내보내기 완료")
        return output

    # ============================================================
    # 위협 행 렌더링
    # ============================================================

    def _render_threat_row(self, threat: dict) -> str:
        """
        개별 위협 정보를 HTML 테이블 행으로 렌더링.
        
        Args:
            threat: 위협 정보 {"type", "severity", "description", "source"}
            
        Returns:
            str: HTML <tr> 태그
        """
        severity = threat.get("severity", "info")
        color = self.SEVERITY_COLORS.get(severity, self.COLORS["text"])

        # 심각도 배지
        badge = (
            f'<span class="severity-badge" style="background: {color}; '
            f'color: #1e1e2e;">{severity.upper()}</span>'
        )

        return f"""
        <tr>
            <td>{badge}</td>
            <td>{self._escape_html(threat.get("type", "unknown"))}</td>
            <td>{self._escape_html(threat.get("description", ""))}</td>
            <td>{self._escape_html(threat.get("source", ""))}</td>
        </tr>"""

    # ============================================================
    # SVG 점수 게이지
    # ============================================================

    def _render_score_gauge(self, score: int) -> str:
        """
        원형 SVG 점수 게이지 렌더링.
        
        원 둘레를 점수 비율만큼 채워서 시각적으로 표시.
        0~40: 빨강, 41~70: 노랑, 71~100: 초록.
        
        Args:
            score: 보안 점수 (0~100)
            
        Returns:
            str: SVG 문자열
        """
        # 색상 결정
        color = self._score_color(score)

        # 원 파라미터
        radius = 54
        circumference = 2 * 3.14159 * radius
        offset = circumference - (score / 100) * circumference

        return f"""
        <svg class="score-gauge" width="140" height="140" viewBox="0 0 120 120">
            <!-- 배경 원 -->
            <circle cx="60" cy="60" r="{radius}"
                    fill="none" stroke="{self.COLORS['surface']}"
                    stroke-width="10" />
            <!-- 점수 원 (애니메이션) -->
            <circle cx="60" cy="60" r="{radius}"
                    fill="none" stroke="{color}"
                    stroke-width="10"
                    stroke-linecap="round"
                    stroke-dasharray="{circumference:.1f}"
                    stroke-dashoffset="{offset:.1f}"
                    transform="rotate(-90 60 60)"
                    style="transition: stroke-dashoffset 0.8s ease;" />
            <!-- 점수 텍스트 -->
            <text x="60" y="56" text-anchor="middle"
                  fill="{color}" font-size="28" font-weight="bold"
                  font-family="system-ui, sans-serif">
                {score}
            </text>
            <text x="60" y="74" text-anchor="middle"
                  fill="{self.COLORS['subtext']}" font-size="12"
                  font-family="system-ui, sans-serif">
                / 100
            </text>
        </svg>"""

    # ============================================================
    # 유틸리티
    # ============================================================

    def _score_color(self, score: int) -> str:
        """점수에 따른 색상 반환"""
        if score >= 71:
            return self.COLORS["green"]
        elif score >= 41:
            return self.COLORS["yellow"]
        else:
            return self.COLORS["red"]

    @staticmethod
    def _escape_html(text: str) -> str:
        """HTML 특수문자 이스케이프"""
        return (
            text.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;")
                .replace('"', "&quot;")
                .replace("'", "&#x27;")
        )

    def _embedded_css(self) -> str:
        """보고서 내장 CSS 스타일"""
        return f"""
        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }}

        body {{
            background: {self.COLORS['bg']};
            color: {self.COLORS['text']};
            font-family: 'Pretendard', 'SF Pro', system-ui, -apple-system, sans-serif;
            line-height: 1.6;
            padding: 20px;
        }}

        .container {{
            max-width: 900px;
            margin: 0 auto;
        }}

        .header {{
            text-align: center;
            padding: 24px 0;
            border-bottom: 2px solid {self.COLORS['surface']};
            margin-bottom: 24px;
        }}

        .header h1 {{
            font-size: 24px;
            color: {self.COLORS['accent']};
            margin-bottom: 4px;
        }}

        .subtitle {{
            color: {self.COLORS['subtext']};
            font-size: 13px;
        }}

        .section {{
            background: {self.COLORS['surface']};
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 16px;
        }}

        .section h2 {{
            font-size: 16px;
            color: {self.COLORS['accent']};
            margin-bottom: 12px;
        }}

        .url-box {{
            background: {self.COLORS['bg']};
            border: 1px solid {self.COLORS['overlay']};
            border-radius: 8px;
            padding: 12px 16px;
            word-break: break-all;
            font-size: 13px;
        }}

        .url-box code {{
            color: {self.COLORS['accent']};
        }}

        .score-section {{
            text-align: center;
        }}

        .score-container {{
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 24px;
        }}

        .score-details {{
            text-align: left;
        }}

        .score-number {{
            font-size: 48px;
            font-weight: bold;
        }}

        .score-label {{
            font-size: 20px;
            color: {self.COLORS['subtext']};
        }}

        .score-range {{
            font-size: 12px;
            color: {self.COLORS['subtext']};
        }}

        .stats-row {{
            display: flex;
            gap: 12px;
            flex-wrap: wrap;
        }}

        .stat-card {{
            flex: 1;
            min-width: 100px;
            background: {self.COLORS['bg']};
            border-radius: 8px;
            padding: 12px;
            text-align: center;
        }}

        .stat-num {{
            display: block;
            font-size: 28px;
            font-weight: bold;
        }}

        .stat-label {{
            display: block;
            font-size: 12px;
            color: {self.COLORS['subtext']};
            text-transform: uppercase;
        }}

        .threat-table {{
            width: 100%;
            border-collapse: collapse;
            font-size: 13px;
        }}

        .threat-table th {{
            background: {self.COLORS['bg']};
            color: {self.COLORS['subtext']};
            padding: 10px 12px;
            text-align: left;
            font-weight: 600;
            text-transform: uppercase;
            font-size: 11px;
        }}

        .threat-table td {{
            padding: 10px 12px;
            border-bottom: 1px solid {self.COLORS['overlay']};
            vertical-align: top;
        }}

        .threat-table tr:hover td {{
            background: rgba(137, 180, 250, 0.05);
        }}

        .severity-badge {{
            display: inline-block;
            padding: 2px 8px;
            border-radius: 4px;
            font-size: 11px;
            font-weight: bold;
            text-transform: uppercase;
        }}

        .no-data {{
            text-align: center;
            color: {self.COLORS['green']};
            padding: 24px !important;
        }}

        .rec-list {{
            list-style: none;
            padding: 0;
        }}

        .rec-item {{
            background: {self.COLORS['bg']};
            border-left: 3px solid {self.COLORS['yellow']};
            padding: 10px 14px;
            margin-bottom: 8px;
            border-radius: 0 8px 8px 0;
            font-size: 13px;
        }}

        .footer {{
            text-align: center;
            padding: 20px 0;
            color: {self.COLORS['subtext']};
            font-size: 12px;
            border-top: 1px solid {self.COLORS['surface']};
            margin-top: 24px;
        }}

        @media (max-width: 600px) {{
            .stats-row {{
                flex-direction: column;
            }}
            .score-container {{
                flex-direction: column;
            }}
        }}
        """
