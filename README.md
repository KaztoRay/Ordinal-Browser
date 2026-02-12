# Ordinal Browser

V8 엔진 기반 보안 브라우저 + LLM Security Agent

## Overview

Ordinal Browser는 V8 JavaScript 엔진을 핵심으로 하는 보안 중심 웹 브라우저입니다.
LLM 기반 보안 에이전트가 내장되어 있어 실시간으로 웹 위협을 분석하고 차단합니다.

## Architecture

```
┌─────────────────────────────────────────────┐
│              Ordinal Browser                │
├──────────┬──────────┬───────────────────────┤
│   UI     │ Network  │   Security Agent      │
│  Layer   │  Layer   │   (LLM-Powered)       │
├──────────┴──────────┴───────────────────────┤
│              V8 Engine Core                 │
├─────────────────────────────────────────────┤
│           Platform Abstraction              │
└─────────────────────────────────────────────┘
```

## Features

### 🔒 Security Agent (LLM)
- 피싱 사이트 실시간 탐지
- 악성 JavaScript 분석 및 차단
- XSS/CSRF/Injection 공격 탐지
- SSL/TLS 인증서 검증
- 개인정보 추적기 차단
- URL 평판 검사

### 🌐 Browser Core
- V8 JavaScript 엔진 기반
- 멀티탭 브라우징
- HTTP/HTTPS 네트워크 스택
- HTML/CSS 파싱 및 렌더링
- 확장 프로그램 지원

### 🤖 AI Features
- 자연어 웹 검색
- 페이지 요약
- 코드 분석 및 취약점 스캔
- 보안 리포트 자동 생성

## Tech Stack

- **Core**: C++ 20
- **JS Engine**: V8
- **UI**: Qt 6
- **Network**: libcurl + OpenSSL
- **AI Agent**: Python + LLM API
- **Build**: CMake 3.20+
- **IPC**: gRPC (C++ ↔ Python)

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## License

Private - Ordinal Project
