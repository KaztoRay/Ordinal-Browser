# Changelog

이 프로젝트의 모든 주요 변경 사항을 기록합니다.
[Keep a Changelog](https://keepachangelog.com/ko/1.1.0/) 형식을 따릅니다.

## [1.2.0] — 2026-02-18

### 🚀 v1.2.0 — 탭 매니저, 퍼포먼스, 리더 모드, 프라이버시 강화

#### 🧩 코어 엔진 강화
- **탭 매니저** (`TabManager`) — 탭 그룹 (색상/라벨), 핀/뮤트, 자동 수면 관리, 검색, 닫힌 탭 복원 (50개 스택), 썸네일, 윈도우 간 드래그, 세션 직렬화
- **퍼포먼스 모니터** (`PerformanceMonitor`) — 탭별 CPU/메모리/네트워크 추적, 프레임 타이밍 (p95/p99), Navigation Timing (W3C), 메모리 압력 감지, 최적화 제안
- **미디어 컨트롤러** (`MediaController`) — 전역 재생/일시정지/볼륨, PiP, 캐스팅 (AirPlay/Chromecast), 오디오 라우팅, OS 미디어 세션 통합

#### 🌐 네트워크 강화
- **DNS 리졸버** (`DnsResolver`) — DNS-over-HTTPS (DoH), 캐싱, DNSSEC
- **프록시 매니저** (`ProxyManager`) — HTTP/SOCKS5 프록시, PAC 스크립트, 자동 감지
- **다운로드 가속기** (`DownloadAccelerator`) — 멀티스레드 세그먼트 다운로드 (최대 8병렬), 이어받기, 대역폭 관리, 우선순위 큐, 상태 직렬화

#### 📖 UI 신규 기능
- **리더 모드** (`ReaderMode`) — 아티클 자동 추출 (CJK 지원), 4테마 (Light/Dark/Sepia/Custom), 3폰트, TTS 연동, 인쇄 지원, 읽기 시간 추정

#### 🔒 프라이버시 강화
- **쿠키 동의 자동 처리** (`ConsentHandler`) — 10개 CMP 자동 감지/거부 (CookieBot, OneTrust, Quantcast, Didomi, Klaro, Osano, TrustArc, Complianz 등), 사이트별 화이트리스트
- **WebRTC 보호** (`WebRtcGuard`) — 5단계 정책 (Default/PublicOnly/ForceRelay/DisableNonProxied/Disabled), IP 유출 방지 JS 인젝션, STUN 차단, ICE 후보 필터링, VPN 인터페이스 감지

### 📊 프로젝트 통계
- **파일 수:** 155+
- **코드 라인:** 58,000+
- **새 파일:** 14개 (C++ 12, Python 2)
- **새 코드:** +6,000줄

[1.2.0]: https://github.com/KaztoRay/OrdinalV8/compare/v1.1.0...v1.2.0

## [1.1.0] — 2026-02-15

### 🚀 v1.1.0 — 확장 프로그램, 테마, 에이전트 강화

#### 🧩 확장 프로그램 시스템
- Chrome 호환 매니페스트, V8 샌드박싱, content script, 메시지 패싱, 스토리지 API

#### 📚 데이터 관리
- 북마크 관리자, 히스토리, 다운로드 관리자, 세션 복구

#### 🔑 프라이버시
- 비밀번호 관리자 (AES-256-GCM), 쿠키 관리자, 자동입력

#### 🛡️ 광고 차단
- EasyList/uBlock 호환 필터 엔진, CSS 요소 숨김

#### 🎨 테마 & 설정
- Light/Dark/System 테마 (Catppuccin 팔레트), 6탭 설정 화면

#### 🤖 에이전트 강화
- URL 평판, CSP/SRI/CORS 분석, HTML 보안 리포트, 실시간 모니터

[1.1.0]: https://github.com/KaztoRay/OrdinalV8/compare/v1.0.0...v1.1.0

## [1.0.0] — 2026-02-13

### 🚀 첫 번째 공식 릴리즈

V8 JavaScript 엔진 기반 보안 브라우저와 LLM Security Agent의 최초 릴리즈입니다.

### ✨ 추가된 기능

#### 코어 엔진
- V8 JavaScript 엔진 통합 (`BrowserApp`, `V8Engine`)
- 페이지 및 탭 관리 시스템 (`Page`, `Tab`)
- 메인 엔트리포인트 (`main.cpp`) — Qt6 + V8 + gRPC 초기화

#### 네트워크 레이어
- HTTP/HTTPS 클라이언트 (`HttpClient`) — libcurl 래퍼, 동기/비동기 요청
- 요청 인터셉터 (`RequestInterceptor`) — 보안 검사 미들웨어 체인
- SSL/TLS 검사기 (`SslInspector`) — 인증서 체인, 프로토콜 버전 분석

#### 보안 모듈 (6종)
- 피싱 탐지기 (`PhishingDetector`) — URL 패턴 분석, 타이포스쿼팅, 호모글리프 탐지
- XSS 분석기 (`XssAnalyzer`) — 반사형/저장형/DOM 기반 XSS 탐지, 인코딩 디코딩
- 스크립트 분석기 (`ScriptAnalyzer`) — 악성 JS 패턴, 크립토마이너, 키로거 탐지
- 개인정보 추적기 (`PrivacyTracker`) — EasyList/EasyPrivacy 호환, 핑거프린팅 차단
- 인증서 검증기 (`CertValidator`) — 체인 검증, HSTS, CT 로그, 인증서 핀닝
- 보안 에이전트 (`SecurityAgent`) — 모든 보안 서브시스템 통합 코디네이터

#### 렌더링 엔진
- HTML5 파서 (`HtmlParser`) — 토크나이저 + 트리 생성기, 상태 머신 기반
- CSS 파서 (`CssParser`) — 셀렉터, 특이성 계산, @media 쿼리
- DOM 트리 (`DomTree`) — Document/Element/Text/Comment 노드, querySelector
- 레이아웃 엔진 (`LayoutEngine`) — 박스 모델, 블록/인라인 레이아웃, 마진 겹침

#### Qt6 네이티브 UI (5종)
- 메인 윈도우 (`MainWindow`) — 메뉴바, 툴바, 탭 스택, 상태바
- 탭 바 (`TabBar`) — 드래그 앤 드롭, 탭 추가/닫기, 컨텍스트 메뉴
- 주소 바 (`AddressBar`) — URL 입력, 보안 상태 아이콘, 자동완성
- 보안 패널 (`SecurityPanel`) — 실시간 위협 표시, 차단 통계
- 개발자 도구 (`DevToolsPanel`) — 콘솔, 네트워크, 소스 탭

#### Python LLM Security Agent
- 보안 에이전트 코어 (`SecurityAgent`) — 비동기 분석 파이프라인, 결과 캐싱
- 피싱 분석기 (`PhishingAnalyzer`) — URL 42개 특징 추출, 콘텐츠 분석, 호모글리프/타이포스쿼팅
- 악성코드 분석기 (`MalwareAnalyzer`) — 16개 패턴 매칭, 난독화 점수, Base64 디코딩
- 개인정보 분석기 (`PrivacyAnalyzer`) — 추적기 DB, 핑거프린팅 API, 픽셀 추적기
- LLM 추론 엔진 (`LLMInference`) — GPT-4 + 로컬 HuggingFace 모델 폴백
- 임베딩 엔진 (`EmbeddingEngine`) — ada-002 + sentence-transformers, 배치 처리
- gRPC API 서버 (`SecurityService`) — 실시간 스트리밍, 헬스 체크
- URL 검사기 (`URLChecker`) — Google Safe Browsing + VirusTotal API
- 위협 데이터베이스 (`ThreatDatabase`) — aiosqlite 기반 비동기 저장
- 특징 추출기 (`FeatureExtractor`) — URL/JS/DOM 특징 벡터 생성

#### 빌드 시스템
- CMake 빌드 설정 (`CMakeLists.txt`) — C++20, Qt6, V8, gRPC, libcurl
- V8 빌드 스크립트 (`build_v8.sh`) — depot_tools 기반 자동 빌드
- 환경 설정 스크립트 (`setup.sh`) — 의존성 자동 설치

#### CI/CD
- GitHub Actions CI (`ci.yml`) — 빌드 + 테스트 (Ubuntu/macOS/Windows)
- 릴리즈 워크플로 (`release.yml`) — 태그 푸시 시 자동 빌드 + 릴리즈
- 6개 플랫폼 지원 (Windows x64/ARM64, macOS Silicon/Intel, Linux x64/ARM64)

#### 패키징 (6종)
- Windows NSIS 인스톨러 (`installer.nsi`)
- macOS DMG 빌더 (`create_dmg.sh`, `Info.plist`)
- Linux DEB 빌더 (`create_deb.sh`)
- Linux AppImage 빌더 (`create_appimage.sh`)
- 데스크톱 엔트리 (`ordinalv8.desktop`)
- CPack 크로스 플랫폼 설정 (`packaging/CMakeLists.txt`)

#### 테스트 (40+ 케이스)
- C++ Google Test — 보안 모듈 15개, 렌더링 16개, 네트워크 10개 테스트
- Python pytest — Agent 5개, 분석기 9개 비동기 테스트
- CI 통합 테스트 자동 실행

#### 문서 (3종)
- 아키텍처 문서 (`docs/architecture.md`)
- V8 통합 가이드 (`docs/v8-integration.md`)
- LLM 보안 에이전트 명세 (`docs/security-agent.md`)

### 📊 프로젝트 통계
- **파일 수:** 80+
- **코드 라인:** 30,000+
- **언어:** C++20, Python 3.12
- **테스트:** 40+ 케이스 (GTest + pytest)
- **커밋:** 17개

[1.0.0]: https://github.com/KaztoRay/ordinalv8/releases/tag/v1.0.0
