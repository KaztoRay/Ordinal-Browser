# OrdinalV8

V8 엔진 기반 보안 브라우저 + 내장 AI 어시스턴트

## Overview

OrdinalV8은 V8 JavaScript 엔진을 핵심으로 하는 보안 중심 웹 브라우저입니다.
내장 AI 어시스턴트가 웹 브라우징, 보안 분석, 번역, 코드 분석을 실시간으로 지원합니다.

## Architecture

```
┌─────────────────────────────────────────────┐
│                OrdinalV8                    │
├──────────┬──────────┬───────────────────────┤
│   UI     │ Network  │   AI Assistant        │
│  Layer   │  Layer   │   (Multi-LLM)         │
├──────────┴──────────┴───────────────────────┤
│        Qt WebEngine (Chromium Core)         │
├─────────────────────────────────────────────┤
│           Platform Abstraction              │
└─────────────────────────────────────────────┘
```

## Features

### 🌐 브라우저 기능
- Qt WebEngine (Chromium) 기반 실제 웹 브라우징
- 탭 관리 (새 탭, 닫기, 전환, 복제, 고정, 수면)
- 주소창 + URL 자동완성 + 검색 엔진 통합
- 뒤로/앞으로/새로고침
- 북마크 관리
- 방문 기록
- 다운로드 관리 (가속 다운로드)
- 설정 페이지
- F12 개발자 도구 (DevTools)
- 리더 모드
- 스크린샷 / PDF 저장
- 세션 복원

### 🤖 AI 어시스턴트
OrdinalV8에 내장된 AI 어시스턴트는 다음 기능을 제공합니다:

- **자연어 질의응답** — 무엇이든 물어보세요
- **페이지 요약** — 현재 웹페이지를 한국어로 요약
- **페이지 번역** — 외국어 페이지를 자연스럽게 번역
- **보안 분석** — SSL, 도메인 신뢰도, 위협 요소 평가
- **코드 분석** — 선택한 코드의 기능, 버그, 개선점 분석
- **검색어 추천** — 더 나은 검색을 위한 제안
- **대화 히스토리** — 이전 대화를 기억하며 맥락 유지
- **다중 LLM 백엔드** — Ollama (로컬), OpenAI, Anthropic, 커스텀 API 지원

#### AI 퀵 액션
| 버튼 | 기능 | 단축키 |
|------|------|--------|
| 📝 요약 | 현재 페이지 요약 | - |
| 🌐 번역 | 페이지 번역 | - |
| 🔒 보안 | 보안 분석 | - |
| 💻 코드 | 코드 분석 | - |

#### AI 명령어
```
/help      — 도움말
/clear     — 대화 초기화
/model     — 현재 모델 정보
/summarize — 현재 페이지 요약
/translate — 페이지 번역
/security  — 보안 분석
/search    — 검색
/settings  — AI 설정
```

### 🔒 보안 기능
- 피싱 사이트 실시간 탐지
- 악성 JavaScript 분석 및 차단
- XSS/CSRF/Injection 공격 탐지
- SSL/TLS 인증서 검증
- 핑거프린팅 방지

### 🛡️ 프라이버시
- 광고 차단 (AdBlock)
- WebRTC 가드
- 쿠키 관리
- 비밀번호 관리자
- 자동 완성 엔진

### 🎨 UI/UX
- 다크/라이트 모드 + 시스템 연동
- URL 텍스트 skyblue 색상
- 사이드바 AI 어시스턴트 (Ctrl+Shift+A)
- 전체 화면 모드 (F11)
- 확대/축소 (Ctrl+/-)

## 빌드

### 요구사항
- CMake 3.24+
- Qt 6.x (WebEngine 포함)
- C++20 컴파일러
- OpenSSL, CURL

### macOS (Apple Silicon)
```bash
brew install qt@6
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@6 -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
open ordinalv8.app
```

## AI 어시스턴트 설정

기본적으로 ChatGPT LLM을 사용합니다:

## 단축키

| 단축키 | 기능 |
|--------|------|
| Ctrl+T | 새 탭 |
| Ctrl+W | 탭 닫기 |
| Ctrl+L | 주소창 포커스 |
| Ctrl+D | 북마크 추가/제거 |
| Ctrl+H | 방문 기록 |
| Ctrl+Shift+B | 북마크 관리 |
| Ctrl+Shift+A | AI 어시스턴트 |
| F12 | 개발자 도구 |
| F11 | 전체 화면 |
| Ctrl+F | 페이지 내 검색 |
| Ctrl+Shift+S | 스크린샷 |
| Ctrl+Shift+P | PDF 저장 |

## License

© 2026 KaztoRay. All rights reserved.
