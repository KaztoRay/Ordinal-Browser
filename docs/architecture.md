# Ordinal Browser — 아키텍처 문서

## 1. 프로젝트 개요

**Ordinal Browser**는 C++20과 Qt6 기반으로 구현된 보안 중심 웹 브라우저입니다.
자체 HTML5 파서, CSS 파서, DOM 트리, 레이아웃 엔진, V8 JavaScript 엔진을
내장하고 있으며, Python LLM 기반 보안 에이전트와 gRPC 양방향 통신을 통해
실시간 위협 분석을 수행합니다.

### 핵심 설계 원칙

- **보안 우선**: 모든 네트워크 요청과 스크립트 실행을 보안 파이프라인이 검사
- **모듈화**: UI, 코어, 네트워크, 보안, 렌더링이 명확히 분리
- **프로세스 격리**: V8 엔진 샌드박싱, Python Agent 별도 프로세스
- **LLM 심층 분석**: GPT-4 기반 피싱/악성코드/프라이버시 분석

### 기술 스택

| 계층 | 기술 |
|------|------|
| UI | Qt6 Widgets (C++) |
| 코어 엔진 | C++20, V8 JavaScript Engine |
| 네트워크 | libcurl, OpenSSL |
| 보안 | C++ 휴리스틱 + Python LLM Agent |
| IPC | gRPC (protobuf) |
| 빌드 | CMake 3.24+ |

---

## 2. 아키텍처 다이어그램

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Ordinal Browser                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                      UI 레이어 (Qt6)                          │  │
│  │  ┌──────────┐ ┌────────────┐ ┌──────────────┐ ┌───────────┐  │  │
│  │  │ TabBar   │ │ AddressBar │ │SecurityPanel │ │DevTools   │  │  │
│  │  │          │ │            │ │              │ │Panel      │  │  │
│  │  └────┬─────┘ └─────┬──────┘ └──────┬───────┘ └─────┬─────┘  │  │
│  │       │              │               │               │        │  │
│  │  ┌────┴──────────────┴───────────────┴───────────────┴─────┐  │  │
│  │  │                  MainWindow                              │  │  │
│  │  └──────────────────────┬───────────────────────────────────┘  │  │
│  └─────────────────────────┼─────────────────────────────────────┘  │
│                            │                                        │
│  ┌─────────────────────────┼─────────────────────────────────────┐  │
│  │                   코어 레이어                                  │  │
│  │  ┌──────────┐  ┌───────┴──────┐  ┌──────────────────────────┐ │  │
│  │  │BrowserApp│  │  Tab / Page  │  │      V8Engine            │ │  │
│  │  │          │←→│              │←→│  (JavaScript 실행)       │ │  │
│  │  └──────────┘  └──────────────┘  └──────────────────────────┘ │  │
│  └───────────────────────┬───────────────────────────────────────┘  │
│                          │                                          │
│  ┌───────────┬───────────┴────────────┬──────────────────────────┐  │
│  │           │                        │                          │  │
│  │  ┌────────┴────────┐  ┌───────────┴──────────┐  ┌──────────┐ │  │
│  │  │  네트워크 레이어  │  │   렌더링 레이어       │  │ 보안     │ │  │
│  │  │                  │  │                      │  │ 레이어   │ │  │
│  │  │ ┌──────────────┐ │  │ ┌──────────────────┐ │  │          │ │  │
│  │  │ │ HttpClient   │ │  │ │ HtmlParser       │ │  │ Phishing │ │  │
│  │  │ │ (libcurl)    │ │  │ │ (토크나이저+트리) │ │  │ Detector │ │  │
│  │  │ └──────────────┘ │  │ └──────────────────┘ │  │          │ │  │
│  │  │ ┌──────────────┐ │  │ ┌──────────────────┐ │  │ Xss      │ │  │
│  │  │ │Request       │ │  │ │ CssParser        │ │  │ Analyzer │ │  │
│  │  │ │Interceptor   │ │  │ │ (셀렉터+속성맵)  │ │  │          │ │  │
│  │  │ │(미들웨어체인) │ │  │ └──────────────────┘ │  │ Script   │ │  │
│  │  │ └──────────────┘ │  │ ┌──────────────────┐ │  │ Analyzer │ │  │
│  │  │ ┌──────────────┐ │  │ │ DomTree          │ │  │          │ │  │
│  │  │ │SslInspector  │ │  │ │ (Document/       │ │  │ Privacy  │ │  │
│  │  │ │(TLS 검사)    │ │  │ │  Element/Text)   │ │  │ Tracker  │ │  │
│  │  │ └──────────────┘ │  │ └──────────────────┘ │  │          │ │  │
│  │  └──────────────────┘  │ ┌──────────────────┐ │  │ Cert     │ │  │
│  │                        │ │ LayoutEngine     │ │  │ Validator│ │  │
│  │                        │ │ (박스 모델/블록/  │ │  │          │ │  │
│  │                        │ │  인라인 레이아웃) │ │  └──────────┘ │  │
│  │                        │ └──────────────────┘ │               │  │
│  │                        └──────────────────────┘               │  │
│  └───────────────────────────────────────────────────────────────┘  │
│                          │                                          │
│                    gRPC 양방향                                      │
│                          │                                          │
├──────────────────────────┼──────────────────────────────────────────┤
│                          ▼                                          │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │              Python LLM Security Agent                        │  │
│  │                                                               │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌────────────────────┐  │  │
│  │  │ Phishing     │  │ Malware      │  │ Privacy            │  │  │
│  │  │ Analyzer     │  │ Analyzer     │  │ Analyzer           │  │  │
│  │  │ (URL 특징    │  │ (패턴 매칭   │  │ (추적기 DB 매칭    │  │  │
│  │  │  추출+LLM)   │  │  +난독화+LLM)│  │  +핑거프린팅 탐지) │  │  │
│  │  └──────────────┘  └──────────────┘  └────────────────────┘  │  │
│  │                                                               │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌────────────────────┐  │  │
│  │  │ LLM Inference│  │ Embedding    │  │ Feature            │  │  │
│  │  │ (GPT-4 +     │  │ Engine       │  │ Extractor          │  │  │
│  │  │  로컬 폴백)  │  │ (ada-002 +   │  │ (URL/DOM/JS        │  │  │
│  │  │              │  │  sentence-tf)│  │  특징 추출)         │  │  │
│  │  └──────────────┘  └──────────────┘  └────────────────────┘  │  │
│  │                                                               │  │
│  │  ┌──────────────┐  ┌──────────────┐                          │  │
│  │  │ gRPC Server  │  │ Threat DB    │                          │  │
│  │  │ (서비스 제공) │  │ (aiosqlite)  │                          │  │
│  │  └──────────────┘  └──────────────┘                          │  │
│  └───────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. 모듈 상세 설명

### 3.1 UI 레이어 (`src/ui/`)

Qt6 Widgets 기반 사용자 인터페이스입니다.

| 클래스 | 파일 | 설명 |
|--------|------|------|
| `MainWindow` | `main_window.h/cpp` | 메인 윈도우. 메뉴바, 툴바, 탭 스택, 상태바를 관리합니다. |
| `TabBar` | `tab_bar.h/cpp` | 탭 바. 탭 생성/삭제/전환, 드래그 정렬을 지원합니다. |
| `AddressBar` | `address_bar.h/cpp` | 주소창. URL 입력, 보안 상태 아이콘(자물쇠), 자동완성을 표시합니다. |
| `SecurityPanel` | `security_panel.h/cpp` | 보안 패널. 현재 페이지의 위협 보고서, 보안 점수, 인증서 정보를 보여줍니다. |
| `DevToolsPanel` | `dev_tools_panel.h/cpp` | 개발자 도구. DOM 트리 뷰어, 콘솔, 네트워크 모니터를 제공합니다. |

### 3.2 코어 레이어 (`src/core/`)

브라우저의 핵심 구조와 V8 JavaScript 엔진 래퍼입니다.

| 클래스 | 파일 | 설명 |
|--------|------|------|
| `BrowserApp` | `browser_app.h/cpp` | 브라우저 애플리케이션 싱글톤. 탭 관리, 설정, 전역 상태를 관리합니다. |
| `Tab` | `tab.h/cpp` | 탭. 하나의 탭 = 하나의 Page + V8 컨텍스트. 히스토리(뒤로/앞으로) 관리. |
| `Page` | `page.h/cpp` | 페이지. URL 로드 → 네트워크 요청 → 파싱 → 렌더링 파이프라인을 실행합니다. |
| `V8Engine` | `v8_engine.h/cpp` | V8 래퍼. Isolate/Context 관리, 스크립트 실행, 네이티브 함수 바인딩, 타임아웃/메모리 제한을 담당합니다. |

### 3.3 네트워크 레이어 (`src/network/`)

HTTP/HTTPS 통신과 요청 보안 검사를 담당합니다.

| 클래스 | 파일 | 설명 |
|--------|------|------|
| `HttpClient` | `http_client.h/cpp` | libcurl 래핑 HTTP 클라이언트. 동기/비동기 요청, 쿠키, 프록시, SSL 지원. |
| `RequestInterceptor` | `request_interceptor.h/cpp` | 미들웨어 체인 기반 요청 인터셉터. 피싱/악성코드/추적기 차단 미들웨어를 체인으로 연결합니다. |
| `SslInspector` | `ssl_inspector.h/cpp` | SSL/TLS 연결 검사기. 프로토콜 버전, 암호 스위트, 인증서 체인, HSTS, OCSP를 검사합니다. |

### 3.4 렌더링 레이어 (`src/rendering/`)

HTML 파싱부터 레이아웃 계산까지의 렌더링 파이프라인입니다.

| 클래스 | 파일 | 설명 |
|--------|------|------|
| `HtmlParser` | `html_parser.h/cpp` | HTML5 호환 파서. 상태 머신 기반 토크나이저(Data, TagOpen, TagName, Attribute 등 20개 상태)와 트리 생성기(열린 요소 스택, 암시적 태그, void 요소 처리). 엔티티 디코딩(`&amp;` → `&`) 지원. |
| `CssParser` | `css_parser.h/cpp` | CSS 파서. 토크나이저, 셀렉터 파싱(타입/클래스/ID/속성/의사 클래스), 특이성(Specificity) 계산, @media 쿼리, 인라인 스타일 파싱. |
| `DomTree` | `dom_tree.h/cpp` | DOM 트리. `DocumentNode` → `ElementNode` / `TextNode` / `CommentNode` 계층 구조. `querySelector`, `getElementById`, 깊이/너비 우선 순회 지원. |
| `LayoutEngine` | `layout_engine.h/cpp` | 레이아웃 엔진. CSS 박스 모델(margin/border/padding/content), 블록/인라인 레이아웃, 마진 겹침(collapsing), CSS 단위 변환(px/em/rem/%/vw/vh), 히트 테스트. |

### 3.5 보안 레이어 (`src/security/`)

C++ 휴리스틱 기반 실시간 위협 탐지 시스템입니다.

| 클래스 | 파일 | 설명 |
|--------|------|------|
| `SecurityAgent` | `security_agent.h/cpp` | 중앙 코디네이터 싱글톤. 모든 보안 서브시스템을 초기화하고 위협 탐지 결과를 통합. gRPC를 통해 Python Agent와 통신. |
| `PhishingDetector` | `phishing_detector.h/cpp` | 피싱 탐지. URL 패턴 분석(IP 주소, 의심 포트, typosquatting), 레벤슈타인 거리 기반 도메인 유사도, 블랙/화이트리스트. |
| `XssAnalyzer` | `xss_analyzer.h/cpp` | XSS 분석. 반사형/저장형/DOM/변이 기반 XSS 탐지. 인라인 이벤트 핸들러, 스크립트 인젝션, 위험 속성, URL 인코딩 디코딩. |
| `ScriptAnalyzer` | `script_analyzer.h/cpp` | 스크립트 분석. 크립토마이너, 키로거, 데이터 탈취, 난독화 탐지. Shannon 엔트로피 계산, 알려진 악성코드 해시 매칭. |
| `PrivacyTracker` | `privacy_tracker.h/cpp` | 프라이버시 보호. EasyList/EasyPrivacy 호환 도메인 블록리스트, 핑거프린팅 API 탐지(Canvas, WebGL, AudioContext, 폰트, Navigator), 픽셀 추적기 차단, 통계. |
| `CertValidator` | `cert_validator.h/cpp` | 인증서 검증. 체인 유효성, 호스트명 매칭(와일드카드), 키 강도, HSTS 정책, Certificate Transparency, 인증서 핀닝. |

### 3.6 Python LLM Security Agent (`agent/`)

GPT-4 기반 심층 보안 분석 에이전트입니다.

| 모듈 | 파일 | 설명 |
|------|------|------|
| `SecurityAgent` | `core/agent.py` | 비동기 분석 파이프라인 관리자. 모든 분석기를 조율하고, LLM 프롬프트 템플릿 관리, 결과 캐싱(TTL 기반). |
| `AgentConfig` | `core/config.py` | Pydantic BaseSettings 기반 설정. LLM, 위협 임계값, 외부 API, DB, gRPC 서버 설정을 환경 변수/.env에서 로드. |
| `PhishingAnalyzer` | `analyzers/phishing_analyzer.py` | URL 특징 추출(40개+), 콘텐츠 특징 추출, 호모글리프 탐지, 타이포스쿼팅(레벤슈타인 거리), Shannon 엔트로피. |
| `MalwareAnalyzer` | `analyzers/malware_analyzer.py` | 정규표현식 기반 악성 패턴 매칭(16개 패턴 그룹), 난독화 점수(변수명 엔트로피, 코드 밀도, 인코딩 비율), Base64 디코딩 검증. |
| `PrivacyAnalyzer` | `analyzers/privacy_analyzer.py` | 60개+ 추적기 패턴(Google, Facebook, Twitter, 한국 추적기), 8개 핑거프린팅 API 탐지, 써드파티 쿠키/픽셀 추적기/데이터 유출 분석. |
| `LLMInference` | `models/inference.py` | GPT-4 API 호출, 로컬 HuggingFace 모델 폴백, LRU 캐싱, 지수 백오프 재시도. |
| `EmbeddingEngine` | `models/embeddings.py` | ada-002 + sentence-transformers 임베딩, 배치 처리, 코사인 유사도 기반 위협 매칭. |
| `URLChecker` | `utils/url_checker.py` | Google Safe Browsing API, VirusTotal API 연동. |
| `FeatureExtractor` | `utils/feature_extractor.py` | URL 42개, DOM 24개, JS 32개 특징 추출기. |
| `ThreatDatabase` | `utils/threat_db.py` | aiosqlite 기반 위협 데이터 영속화. |
| `gRPC Server` | `api/grpc_server.py` | SecurityService gRPC 서버. AnalyzeUrl, AnalyzeScript, AnalyzePage, StreamThreats, HealthCheck RPC 구현. |

---

## 4. 데이터 플로우

사용자가 URL을 입력하고 페이지가 렌더링되기까지의 전체 데이터 흐름입니다.

```
사용자: URL 입력
    │
    ▼
┌─────────────────┐
│ AddressBar      │  URL 파싱 및 유효성 검사
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Tab / Page      │  네비게이션 시작, 히스토리 업데이트
└────────┬────────┘
         │
         ▼
┌─────────────────┐          ┌──────────────────────┐
│ HttpClient      │─────────→│ RequestInterceptor   │
│ (HTTP 요청 준비)│          │ (미들웨어 체인 실행)  │
└────────┬────────┘          │                      │
         │                   │  1. 피싱 URL 검사    │
         │                   │  2. 추적기 차단       │
         │                   │  3. 혼합 콘텐츠 검사  │
         │                   │  4. 블랙리스트 검사   │
         │                   └──────────┬───────────┘
         │                              │
         │                   ┌──────────▼───────────┐
         │                   │ 차단? ──→ 경고 페이지 │
         │                   │ 허용? ──→ 계속        │
         │                   └──────────┬───────────┘
         │                              │
         ▼                              ▼
┌─────────────────┐          ┌──────────────────────┐
│ SslInspector    │          │ SecurityAgent (C++)   │
│ (TLS 검사)      │─────────→│ URL 분석 요청        │
└────────┬────────┘          │                      │
         │                   │  ┌─ PhishingDetector  │
         │                   │  ├─ XssAnalyzer       │
         │                   │  ├─ ScriptAnalyzer    │
         │                   │  ├─ PrivacyTracker    │
         │                   │  └─ CertValidator     │
         │                   └──────────┬───────────┘
         │                              │
         │                     gRPC (비동기)
         │                              │
         │                   ┌──────────▼───────────┐
         │                   │ Python LLM Agent     │
         │                   │ (심층 분석)           │
         │                   │                      │
         │                   │  URL → 특징 추출     │
         │                   │  → 분석기 라우팅      │
         │                   │  → LLM 추론          │
         │                   │  → ThreatReport 생성  │
         │                   └──────────┬───────────┘
         │                              │
         │                   위협 보고서 반환
         │                              │
         ▼                              ▼
┌─────────────────┐          ┌──────────────────────┐
│ HTTP 응답 수신   │          │ SecurityPanel        │
│ (HTML + 헤더)    │          │ (위협 보고서 표시)   │
└────────┬────────┘          └──────────────────────┘
         │
         ▼
┌─────────────────┐
│ HtmlParser      │  HTML 토크나이징 → 트리 빌딩
│ (상태 머신 파서) │  엔티티 디코딩, void 요소 처리
└────────┬────────┘
         │
         ├─── <style> ──→ ┌─────────────────┐
         │                │ CssParser        │
         │                │ (셀렉터 파싱,    │
         │                │  특이성 계산)     │
         │                └────────┬────────┘
         │                         │
         ▼                         ▼
┌─────────────────┐     CSS 규칙 목록
│ DomTree         │          │
│ (DOM 트리 구축)  │          │
└────────┬────────┘          │
         │                   │
         ├───────────────────┘
         ▼
┌─────────────────┐
│ LayoutEngine    │  스타일 계산 (캐스케이딩 + 상속)
│ (레이아웃 계산)  │  박스 모델 해석, 블록/인라인 배치
└────────┬────────┘
         │
         ├─── <script> ──→ ┌─────────────────┐
         │                 │ V8Engine         │
         │                 │ (JavaScript 실행) │
         │                 │ 타임아웃 & 메모리 │
         │                 │ 제한 적용         │
         │                 └─────────────────┘
         │
         ▼
┌─────────────────┐
│ UI 렌더링       │  LayoutBox 트리 → 화면 페인팅
│ (MainWindow)    │  
└─────────────────┘
```

---

## 5. IPC: gRPC 양방향 통신

### 5.1 프로토콜 정의

C++ 브라우저 코어와 Python LLM Agent 사이의 통신은 gRPC를 사용합니다.
프로토콜 버퍼 정의는 `agent/proto/security.proto`에 있습니다.

```protobuf
service SecurityService {
    rpc AnalyzeUrl(AnalysisRequest) returns (AnalysisResponse);
    rpc AnalyzeScript(AnalysisRequest) returns (AnalysisResponse);
    rpc AnalyzePage(AnalysisRequest) returns (PageSecurityReport);
    rpc GetSecurityReport(SecurityReportRequest) returns (SecurityReportResponse);
    rpc StreamThreats(StreamRequest) returns (stream ThreatReport);
    rpc HealthCheck(HealthCheckRequest) returns (HealthCheckResponse);
}
```

### 5.2 메시지 흐름

```
C++ Browser Core                    Python LLM Agent
      │                                    │
      │  ── AnalyzeUrl(url) ──────────→   │
      │                                    │  특징 추출
      │                                    │  PhishingAnalyzer 실행
      │                                    │  LLM 추론 (GPT-4)
      │  ←── AnalysisResponse ──────────  │
      │      (ThreatReport, 분석 시간)     │
      │                                    │
      │  ── AnalyzePage(url, html) ────→  │
      │                                    │  병렬 분석:
      │                                    │    피싱 + 악성코드 + 프라이버시
      │  ←── PageSecurityReport ────────  │
      │                                    │
      │  ── StreamThreats(필터) ────────→  │
      │  ←── stream ThreatReport ───────  │  실시간 위협 스트리밍
      │  ←── stream ThreatReport ───────  │
      │       ...                          │
      │                                    │
      │  ── HealthCheck ────────────────→  │
      │  ←── HealthCheckResponse ───────  │
      │      (상태, 캐시 크기, 버전)       │
```

### 5.3 주요 메시지 구조

- **AnalysisRequest**: URL, HTML 콘텐츠, 스크립트 코드, LLM 사용 여부, 메타데이터
- **AnalysisResponse**: 성공 여부, ThreatReport, 분석 시간, 캐시 여부
- **ThreatReport**: URL, 전체 위협 수준(SAFE~CRITICAL), 점수(0~1), 상세 목록, 권장 조치
- **ThreatDetail**: 위협 유형(PHISHING/MALWARE/XSS/PRIVACY/CERT), 수준, 신뢰도, 설명, 지표
- **PageSecurityReport**: 위협 보고서 + 보안 점수 + 피싱/악성코드/프라이버시 상세 분석

---

## 6. 보안 모델

### 6.1 V8 샌드박싱

각 탭의 JavaScript는 독립된 V8 Isolate에서 실행됩니다.

```
┌─ Tab 1 ─────────────┐  ┌─ Tab 2 ─────────────┐
│  Isolate A           │  │  Isolate B           │
│  ┌─ Context ───────┐ │  │  ┌─ Context ───────┐ │
│  │  전역 객체       │ │  │  │  전역 객체       │ │
│  │  window, console │ │  │  │  window, console │ │
│  └─────────────────┘ │  │  └─────────────────┘ │
│                       │  │                       │
│  ResourceConstraints  │  │  ResourceConstraints  │
│  - max_heap: 512MB   │  │  - max_heap: 512MB   │
│  - max_old_space:    │  │  - max_old_space:    │
│    256MB             │  │    256MB             │
│                       │  │                       │
│  타임아웃: 5000ms    │  │  타임아웃: 5000ms    │
│  (TerminateExecution) │  │  (TerminateExecution) │
└───────────────────────┘  └───────────────────────┘
```

- **메모리 격리**: 각 Isolate는 독립된 힙을 가짐
- **메모리 제한**: `ResourceConstraints`로 최대 힙 크기 제한
- **실행 타임아웃**: 별도 스레드에서 `TerminateExecution()` 호출
- **네이티브 함수 제한**: 허용된 API만 `FunctionTemplate`으로 노출

### 6.2 프로세스 격리

```
┌──────────────────────┐     ┌──────────────────────┐
│  Browser Process     │     │  Agent Process        │
│  (C++)               │     │  (Python)             │
│                      │     │                       │
│  - UI 렌더링         │     │  - LLM 추론           │
│  - 네트워크 요청     │◄──►│  - 임베딩 계산         │
│  - V8 실행           │gRPC │  - 위협 DB 관리       │
│  - 보안 검사         │     │  - 외부 API 호출       │
└──────────────────────┘     └──────────────────────┘
```

- **C++ 코어**: 브라우저 메인 프로세스. UI, 네트워크, V8 실행
- **Python Agent**: 별도 프로세스. LLM 추론, 심층 분석
- **gRPC 통신**: 프로세스 간 격리 유지, 크래시 전파 방지

### 6.3 네트워크 보안

1. **미들웨어 체인**: 모든 요청이 `RequestInterceptor`의 미들웨어 체인을 통과
2. **SSL/TLS 검사**: `SslInspector`가 TLS 버전, 암호 스위트, 인증서 유효성 검사
3. **HSTS 강제**: `CertValidator`가 HSTS 프리로드 목록 관리
4. **인증서 핀닝**: 알려진 도메인의 공개키 핀 검증
5. **추적기 차단**: `PrivacyTracker`가 EasyList/EasyPrivacy 기반 도메인 블록

---

## 7. 빌드 방법

### 7.1 필수 의존성

| 패키지 | 최소 버전 | 용도 |
|--------|----------|------|
| CMake | 3.24+ | 빌드 시스템 |
| Qt6 | 6.5+ | UI 프레임워크 |
| OpenSSL | 3.0+ | SSL/TLS |
| libcurl | 7.80+ | HTTP 클라이언트 |
| V8 | 12.0+ | JavaScript 엔진 (선택) |
| gRPC | 1.50+ | IPC (선택) |
| Python | 3.11+ | LLM Agent |

### 7.2 macOS ARM64 빌드

```bash
# 의존성 설치
brew install cmake qt@6 openssl curl grpc protobuf

# 빌드 디렉토리 생성
cd ~/Desktop/ordinal-browser
mkdir -p build && cd build

# CMake 설정
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)

# 빌드
cmake --build . -j$(sysctl -n hw.ncpu)
```

### 7.3 V8 엔진 빌드 (선택)

V8 빌드는 `scripts/build_v8.sh`를 참조하세요.
상세한 V8 빌드 가이드는 [docs/v8-integration.md](v8-integration.md)를 참조하세요.

### 7.4 Python Agent 설정

```bash
cd agent
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

# gRPC proto 컴파일
python -m grpc_tools.protoc \
  --proto_path=proto \
  --python_out=proto \
  --grpc_python_out=proto \
  proto/security.proto
```

---

## 8. 디렉토리 구조

```
ordinal-browser/
├── CMakeLists.txt                  # 루트 CMake 빌드 설정
├── README.md                       # 프로젝트 README
├── .gitignore
│
├── src/                            # C++ 소스 코드
│   ├── main.cpp                    # 앱 엔트리포인트 (Qt6 + V8 + gRPC 초기화)
│   │
│   ├── core/                       # 코어 엔진
│   │   ├── browser_app.h/cpp       # 브라우저 애플리케이션 싱글톤
│   │   ├── tab.h/cpp               # 탭 관리 (히스토리, 네비게이션)
│   │   ├── page.h/cpp              # 페이지 (로드 → 파싱 → 렌더링)
│   │   └── v8_engine.h/cpp         # V8 JavaScript 엔진 래퍼
│   │
│   ├── network/                    # 네트워크 레이어
│   │   ├── http_client.h/cpp       # libcurl HTTP/HTTPS 클라이언트
│   │   ├── request_interceptor.h/cpp # 미들웨어 체인 요청 인터셉터
│   │   └── ssl_inspector.h/cpp     # SSL/TLS 연결 검사기
│   │
│   ├── rendering/                  # 렌더링 엔진
│   │   ├── html_parser.h/cpp       # HTML5 토크나이저 + 트리 생성기
│   │   ├── css_parser.h/cpp        # CSS 파서 (셀렉터, 특이성, @media)
│   │   ├── dom_tree.h/cpp          # DOM 트리 (Document/Element/Text/Comment)
│   │   └── layout_engine.h/cpp     # 박스 모델 레이아웃 엔진
│   │
│   ├── security/                   # C++ 보안 모듈
│   │   ├── security_agent.h/cpp    # 중앙 코디네이터 (싱글톤)
│   │   ├── phishing_detector.h/cpp # 피싱 탐지 (URL 패턴, typosquatting)
│   │   ├── xss_analyzer.h/cpp      # XSS 분석 (반사형/저장형/DOM)
│   │   ├── script_analyzer.h/cpp   # 악성 스크립트 분석 (난독화, 마이닝)
│   │   ├── privacy_tracker.h/cpp   # 프라이버시 추적기 차단
│   │   └── cert_validator.h/cpp    # SSL/TLS 인증서 검증기
│   │
│   └── ui/                         # Qt6 UI 위젯
│       ├── main_window.h/cpp       # 메인 윈도우
│       ├── tab_bar.h/cpp           # 탭 바
│       ├── address_bar.h/cpp       # 주소창
│       ├── security_panel.h/cpp    # 보안 패널
│       └── dev_tools_panel.h/cpp   # 개발자 도구 패널
│
├── agent/                          # Python LLM Security Agent
│   ├── __init__.py
│   ├── requirements.txt            # Python 의존성
│   │
│   ├── core/                       # 코어 모듈
│   │   ├── __init__.py
│   │   ├── agent.py                # SecurityAgent 메인 클래스
│   │   └── config.py               # Pydantic 기반 설정
│   │
│   ├── analyzers/                  # 분석기
│   │   ├── __init__.py
│   │   ├── phishing_analyzer.py    # 피싱 분석 (URL + 콘텐츠)
│   │   ├── malware_analyzer.py     # 악성코드 분석 (패턴 + 난독화)
│   │   └── privacy_analyzer.py     # 프라이버시 분석 (추적기 + 핑거프린팅)
│   │
│   ├── models/                     # ML/LLM 모델
│   │   ├── __init__.py
│   │   ├── inference.py            # LLM 추론 (GPT-4 + 로컬 폴백)
│   │   └── embeddings.py           # 임베딩 엔진 (ada-002 + sentence-tf)
│   │
│   ├── utils/                      # 유틸리티
│   │   ├── __init__.py
│   │   ├── url_checker.py          # 외부 API (GSB, VirusTotal)
│   │   ├── feature_extractor.py    # 특징 추출기 (URL/DOM/JS)
│   │   └── threat_db.py            # 위협 데이터베이스 (aiosqlite)
│   │
│   ├── api/                        # gRPC API
│   │   ├── __init__.py
│   │   └── grpc_server.py          # SecurityService 서버
│   │
│   ├── proto/                      # Protocol Buffers
│   │   └── security.proto          # gRPC 서비스 정의
│   │
│   └── tests/                      # Python 테스트
│       ├── __init__.py
│       ├── conftest.py             # pytest 픽스처
│       ├── test_agent.py           # SecurityAgent 테스트
│       └── test_analyzers.py       # 분석기 테스트
│
├── tests/                          # C++ 테스트 (Google Test)
│   ├── CMakeLists.txt
│   ├── test_security.cpp           # 보안 모듈 테스트
│   ├── test_rendering.cpp          # 렌더링 모듈 테스트
│   └── test_network.cpp            # 네트워크 모듈 테스트
│
├── scripts/                        # 빌드/설정 스크립트
│   ├── build_v8.sh                 # V8 빌드 스크립트
│   └── setup.sh                    # 개발 환경 설정 스크립트
│
└── docs/                           # 문서
    ├── architecture.md             # 아키텍처 문서 (이 파일)
    ├── security-agent.md           # LLM 보안 에이전트 명세
    └── v8-integration.md           # V8 엔진 통합 가이드
```

---

## 부록: 버전 정보

- **프로젝트 버전**: 0.1.0
- **C++ 표준**: C++20
- **대상 플랫폼**: macOS ARM64 (Apple Silicon)
- **라이선스**: MIT
