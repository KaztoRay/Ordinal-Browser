# Changelog

이 프로젝트의 모든 주요 변경 사항을 기록합니다.
[Keep a Changelog](https://keepachangelog.com/ko/1.1.0/) 형식을 따릅니다.

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
- 데스크톱 엔트리 (`ordinal-browser.desktop`)
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

[1.0.0]: https://github.com/KaztoRay/ordinal-browser/releases/tag/v1.0.0
