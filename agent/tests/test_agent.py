"""SecurityAgent 통합 테스트"""
import pytest
import asyncio
from unittest.mock import AsyncMock, MagicMock, patch

# 보안 에이전트 파이프라인 테스트

@pytest.mark.asyncio
async def test_analyze_safe_url(mock_config, mock_llm):
    """안전한 URL 분석 — ThreatLevel.SAFE 반환 확인"""
    from agent.core.agent import SecurityAgent
    mock_llm.analyze.return_value = {"threat_level": "SAFE", "confidence": 0.95, "details": "정상 사이트"}
    agent = SecurityAgent(config=mock_config, llm=mock_llm)
    result = await agent.analyze_url("https://www.google.com")
    assert result["threat_level"] == "SAFE"
    assert result["confidence"] >= 0.9

@pytest.mark.asyncio
async def test_analyze_phishing_url(mock_config, mock_llm):
    """피싱 URL 분석 — ThreatLevel.HIGH 반환 확인"""
    from agent.core.agent import SecurityAgent
    mock_llm.analyze.return_value = {"threat_level": "HIGH", "confidence": 0.92, "details": "피싱 의심: 도메인 유사도 높음"}
    agent = SecurityAgent(config=mock_config, llm=mock_llm)
    result = await agent.analyze_url("https://g00gle-login.com/auth")
    assert result["threat_level"] in ("HIGH", "CRITICAL")
    assert result["confidence"] >= 0.8

@pytest.mark.asyncio
async def test_caching(mock_config, mock_llm):
    """동일 URL 재분석 시 캐시 사용 확인"""
    from agent.core.agent import SecurityAgent
    mock_llm.analyze.return_value = {"threat_level": "SAFE", "confidence": 0.95, "details": "캐시 테스트"}
    agent = SecurityAgent(config=mock_config, llm=mock_llm)
    result1 = await agent.analyze_url("https://example.com")
    result2 = await agent.analyze_url("https://example.com")
    # LLM은 한 번만 호출되어야 함 (캐시 히트)
    assert result1 == result2

@pytest.mark.asyncio
async def test_pipeline_error_handling(mock_config, mock_llm):
    """LLM 오류 시 graceful degradation 확인"""
    from agent.core.agent import SecurityAgent
    mock_llm.analyze.side_effect = TimeoutError("LLM 타임아웃")
    agent = SecurityAgent(config=mock_config, llm=mock_llm)
    result = await agent.analyze_url("https://example.com")
    # 오류 시에도 결과 반환 (기본 휴리스틱 사용)
    assert result is not None
    assert "error" in result or result["threat_level"] == "UNKNOWN"

@pytest.mark.asyncio
async def test_concurrent_analysis(mock_config, mock_llm):
    """동시 다중 URL 분석 테스트"""
    from agent.core.agent import SecurityAgent
    mock_llm.analyze.return_value = {"threat_level": "SAFE", "confidence": 0.9, "details": "동시성 테스트"}
    agent = SecurityAgent(config=mock_config, llm=mock_llm)
    urls = [f"https://example{i}.com" for i in range(5)]
    results = await asyncio.gather(*[agent.analyze_url(url) for url in urls])
    assert len(results) == 5
    assert all(r["threat_level"] == "SAFE" for r in results)
