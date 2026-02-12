# LLM Security Agent 명세서

## 1. 에이전트 개요

Ordinal Browser의 **LLM Security Agent**는 Python으로 구현된 비동기 보안 분석 시스템입니다.
GPT-4(또는 로컬 HuggingFace 모델) 기반 추론과 규칙 기반 분석을 결합하여
피싱, 악성코드, XSS, 프라이버시 침해를 실시간으로 탐지합니다.

### 핵심 특징

- **비동기 파이프라인**: `asyncio` 기반으로 여러 분석기를 병렬 실행
- **LLM Chain-of-Thought**: GPT-4에 체계적인 프롬프트를 전달하여 추론 기반 분석
- **다층 방어**: 규칙 기반(빠른) + LLM 기반(정확한) 이중 분석
- **결과 캐싱**: TTL 기반 캐시로 중복 분석 방지
- **gRPC 서버**: C++ 코어와 양방향 통신, 실시간 위협 스트리밍

---

## 2. 아키텍처

```
┌─────────────────────────────────────────────────────────────────┐
│                    Python LLM Security Agent                     │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │                    gRPC Server Layer                         │ │
│  │  SecurityService (AnalyzeUrl, AnalyzeScript, AnalyzePage,   │ │
│  │                   StreamThreats, HealthCheck)                │ │
│  └──────────────────────────┬──────────────────────────────────┘ │
│                             │                                    │
│  ┌──────────────────────────▼──────────────────────────────────┐ │
│  │                  SecurityAgent (코어)                        │ │
│  │  - 분석 파이프라인 조율                                      │ │
│  │  - LLM 프롬프트 템플릿 관리                                  │ │
│  │  - 결과 캐싱 (TTL + 최대 크기)                               │ │
│  │  - 권장 사항 생성                                            │ │
│  └──────────┬──────────────┬──────────────┬─────────────────────┘ │
│             │              │              │                       │
│  ┌──────────▼────┐ ┌──────▼──────┐ ┌────▼───────────┐           │
│  │ Phishing      │ │ Malware     │ │ Privacy        │           │
│  │ Analyzer      │ │ Analyzer    │ │ Analyzer       │           │
│  │               │ │             │ │                │           │
│  │ - URL 특징    │ │ - 패턴 매칭 │ │ - 추적기 DB    │           │
│  │   추출(40+)   │ │   (16그룹)  │ │   매칭(60+)    │           │
│  │ - 호모글리프  │ │ - 난독화    │ │ - 핑거프린팅   │           │
│  │ - 타이포스쿼팅│ │   점수 계산 │ │   API 탐지(8+) │           │
│  │ - 콘텐츠 분석 │ │ - Base64    │ │ - 픽셀 추적기  │           │
│  │ - LLM 분류   │ │   디코딩    │ │ - 데이터 유출  │           │
│  └───────────────┘ │ - LLM 행동  │ │   위험 분석    │           │
│                    │   예측      │ └────────────────┘           │
│                    └─────────────┘                               │
│                                                                  │
│  ┌────────────────┐ ┌───────────────┐ ┌──────────────────────┐  │
│  │ LLM Inference  │ │ Embedding     │ │ Feature Extractor    │  │
│  │                │ │ Engine        │ │                      │  │
│  │ GPT-4 API     │ │ ada-002       │ │ URL: 42개 특징       │  │
│  │ + 로컬 폴백   │ │ + sentence-   │ │ DOM: 24개 특징       │  │
│  │ + LRU 캐싱    │ │   transformers│ │ JS:  32개 특징       │  │
│  │ + 지수 백오프  │ │ + 배치 처리   │ │                      │  │
│  └────────────────┘ └───────────────┘ └──────────────────────┘  │
│                                                                  │
│  ┌────────────────┐ ┌───────────────┐                           │
│  │ URL Checker    │ │ Threat DB     │                           │
│  │ GSB + VT API   │ │ aiosqlite     │                           │
│  └────────────────┘ └───────────────┘                           │
└──────────────────────────────────────────────────────────────────┘
```

---

## 3. 분석 파이프라인

### 3.1 URL 분석 파이프라인

```
URL 입력
  │
  ▼
┌───────────────────┐
│ 캐시 확인         │──→ 캐시 히트 ──→ 캐시된 ThreatReport 반환
│ (MD5 해시 키)     │
└────────┬──────────┘
         │ 캐시 미스
         ▼
┌───────────────────┐
│ URL 특징 추출     │
│                   │
│ - 도메인 분석     │
│ - 엔트로피 계산   │
│ - 서브도메인 수   │
│ - IP 주소 여부    │
│ - TLD 의심도      │
│ - 호모글리프      │
│ - 타이포스쿼팅    │
│   (레벤슈타인)    │
│ - 브랜드명 포함   │
│ - 특수문자 비율   │
└────────┬──────────┘
         │
         ▼
┌───────────────────┐
│ PhishingAnalyzer  │
│ (규칙 기반)       │
│                   │
│ 가중치 합산:      │
│ - IP 주소: +0.30  │
│ - 호모글리프: +0.30│
│ - 타이포스쿼팅:   │
│   +0.25~0.35      │
│ - @ 기호: +0.20   │
│ - 의심 TLD: +0.15 │
│ - ...             │
└────────┬──────────┘
         │
         ▼
┌───────────────────┐
│ LLM 추론 (선택)  │
│                   │
│ 시스템 프롬프트:  │
│ "웹 보안 전문가"  │
│                   │
│ 사용자 프롬프트:  │
│ URL + 42개 특징   │
│                   │
│ 응답: JSON        │
│ {threat_level,    │
│  confidence,      │
│  reasoning, ...}  │
└────────┬──────────┘
         │
         ▼
┌───────────────────┐
│ ThreatReport 생성 │
│                   │
│ - 점수 재계산     │
│ - 권장 사항 생성  │
│ - 캐시 저장       │
└───────────────────┘
```

### 3.2 페이지 종합 분석 파이프라인

```
URL + HTML 입력
  │
  ▼
┌─────────────────────────────────────────────────────┐
│            asyncio.gather (병렬 실행)                │
│                                                     │
│  ┌─────────────┐  ┌───────────┐  ┌───────────────┐ │
│  │ Phishing    │  │ Malware   │  │ Privacy       │ │
│  │ URL 분석    │  │ HTML 분석 │  │ 페이지 분석   │ │
│  ├─────────────┤  ├───────────┤  ├───────────────┤ │
│  │ Phishing    │  │           │  │               │ │
│  │ 콘텐츠 분석 │  │           │  │               │ │
│  └──────┬──────┘  └─────┬─────┘  └──────┬────────┘ │
│         │               │               │          │
└─────────┼───────────────┼───────────────┼──────────┘
          │               │               │
          ▼               ▼               ▼
      ThreatDetail   ThreatDetail    ThreatDetail
          │               │               │
          └───────────────┼───────────────┘
                          │
                          ▼
                  ┌───────────────┐
                  │ LLM 페이지    │
                  │ 종합 분석     │
                  │ (선택적)      │
                  └───────┬───────┘
                          │
                          ▼
                  ThreatReport (통합)
                  - overall_level
                  - overall_score
                  - details[]
                  - recommendations[]
```

---

## 4. 프롬프트 템플릿

### 4.1 피싱 분석 프롬프트

**시스템 프롬프트:**
```
당신은 웹 보안 전문가입니다. URL을 분석하여 피싱, 악성코드,
의심스러운 패턴을 탐지합니다. 반드시 JSON 형식으로 응답하세요.

응답 형식:
{
  "threat_level": "SAFE|LOW|MEDIUM|HIGH|CRITICAL",
  "threat_types": ["phishing", "malware", ...],
  "confidence": 0.0~1.0,
  "reasoning": "분석 근거",
  "indicators": ["지표1", "지표2"],
  "recommendation": "권장 조치"
}
```

**사용자 프롬프트:**
```
다음 URL의 보안 위협을 분석하세요:

URL: {url}
도메인: {domain}
서브도메인 수: {subdomain_count}
URL 길이: {url_length}
특수 문자 비율: {special_char_ratio:.2f}
IP 주소 사용 여부: {uses_ip}
HTTPS 여부: {is_https}
URL 엔트로피: {entropy:.2f}

이 URL이 피싱이나 악성 사이트일 가능성을 평가하세요.
```

### 4.2 악성코드 분석 프롬프트

**시스템 프롬프트:**
```
당신은 JavaScript 보안 분석 전문가입니다. 제공된 코드에서
악성 패턴, 난독화, 데이터 유출, 크립토마이닝 등을 탐지합니다.
반드시 JSON 형식으로 응답하세요.

응답 형식:
{
  "threat_level": "SAFE|LOW|MEDIUM|HIGH|CRITICAL",
  "malware_type": "none|obfuscation|data_exfil|crypto_miner|exploit",
  "confidence": 0.0~1.0,
  "reasoning": "chain-of-thought 분석",
  "suspicious_patterns": ["패턴1", "패턴2"],
  "behavior_prediction": "예측되는 동작"
}
```

**사용자 프롬프트:**
```
다음 JavaScript 코드의 보안 위협을 분석하세요:

```javascript
{code}
```

코드 통계:
- eval() 사용 횟수: {eval_count}
- document.write() 사용 횟수: {doc_write_count}
- 인코딩된 문자열 수: {encoded_string_count}
- 변수명 엔트로피: {var_entropy:.2f}
- 난독화 점수: {obfuscation_score:.2f}

이 코드가 악성인지 분석하세요. chain-of-thought로 추론하세요.
```

### 4.3 프라이버시(페이지 종합) 분석 프롬프트

**시스템 프롬프트:**
```
당신은 웹 페이지 보안 분석 전문가입니다.
HTML 콘텐츠와 URL을 종합적으로 분석하여
피싱, XSS, 악성코드, 프라이버시 침해를 탐지합니다.
반드시 JSON 형식으로 응답하세요.

응답 형식:
{
  "overall_threat_level": "SAFE|LOW|MEDIUM|HIGH|CRITICAL",
  "threats": [
    {
      "type": "phishing|malware|xss|privacy",
      "level": "SAFE|LOW|MEDIUM|HIGH|CRITICAL",
      "confidence": 0.0~1.0,
      "description": "설명"
    }
  ],
  "security_score": 0~100,
  "recommendations": ["권장1", "권장2"]
}
```

**사용자 프롬프트:**
```
다음 웹 페이지의 보안을 종합 분석하세요:

URL: {url}
페이지 제목: {title}

HTML 요약:
- 폼 수: {form_count}
- 비밀번호 필드 수: {password_field_count}
- 외부 스크립트 수: {external_script_count}
- 외부 리소스 비율: {external_resource_ratio:.2f}
- iframe 수: {iframe_count}
- 숨겨진 요소 수: {hidden_element_count}

HTML 발췌 (첫 2000자):
```html
{html_snippet}
```

종합적인 보안 위협 분석을 수행하세요.
```

---

## 5. 위협 분류 매트릭스

위협은 **수준(Level)** × **유형(Type)** 2차원으로 분류됩니다.

### 5.1 위협 수준 (ThreatLevel)

| 수준 | 값 | 점수 범위 | 설명 | 대응 |
|------|-----|----------|------|------|
| `SAFE` | 0 | 0.00 ~ 0.29 | 안전 | 정상 이용 |
| `LOW` | 1 | 0.30 ~ 0.49 | 낮은 위험 | 정보 표시 |
| `MEDIUM` | 2 | 0.50 ~ 0.64 | 중간 위험 | 경고 표시 |
| `HIGH` | 3 | 0.65 ~ 0.84 | 높은 위험 | 사용자 확인 요청 |
| `CRITICAL` | 4 | 0.85 ~ 1.00 | 치명적 | 자동 차단 |

### 5.2 위협 유형 (ThreatType)

| 유형 | proto 값 | 분석기 | 주요 탐지 패턴 |
|------|----------|--------|--------------|
| `PHISHING` | 0 | PhishingAnalyzer | IP 주소 URL, 타이포스쿼팅, 호모글리프, 의심 TLD, 로그인 폼+외부 리소스 |
| `MALWARE` | 1 | MalwareAnalyzer | eval 체인, 난독화, Base64 실행 코드, 크립토마이닝, 키로거, 데이터 유출 |
| `XSS` | 2 | XssAnalyzer (C++) | `<script>` 인젝션, 이벤트 핸들러, DOM 싱크, URL 인코딩 페이로드 |
| `PRIVACY` | 3 | PrivacyAnalyzer | 추적기 도메인, Canvas/WebGL/AudioContext 핑거프린팅, 픽셀 추적기 |
| `CERT` | 4 | CertValidator (C++) | 만료 인증서, 자체 서명, 약한 키, HSTS 미설정, 체인 불일치 |

### 5.3 위협 매트릭스

```
                PHISHING    MALWARE     XSS         PRIVACY     CERT
            ┌───────────┬───────────┬───────────┬───────────┬───────────┐
 CRITICAL   │ 호모글리프│ 크립토    │ 저장형XSS │ WebRTC IP │ 만료+체인 │
            │ +로그인폼 │ 마이너    │ + 데이터  │ 유출+다중 │ 불일치    │
            │           │ +키로거   │   탈취    │ 핑거프린팅│           │
            ├───────────┼───────────┼───────────┼───────────┼───────────┤
 HIGH       │ 타이포    │ eval+     │ 반사형XSS │ 10+추적기 │ 자체 서명 │
            │ 스쿼팅    │ Base64    │ + 이벤트  │ +핑거     │ +약한 키  │
            │ +피싱키워드│ 실행코드  │   핸들러  │ 프린팅    │           │
            ├───────────┼───────────┼───────────┼───────────┼───────────┤
 MEDIUM     │ IP 주소   │ 난독화    │ DOM 기반  │ 5+추적기  │ 만료 임박 │
            │ +의심TLD  │ 점수>0.5  │ XSS 패턴  │ +쿠키 추적│ +HSTS 없음│
            ├───────────┼───────────┼───────────┼───────────┼───────────┤
 LOW        │ 긴 URL    │ eval 단독 │ URL 파라미│ 1~4 추적기│ 약한 서명 │
            │ +높은     │ 사용      │ 터 의심   │           │ 알고리즘  │
            │ 엔트로피  │           │           │           │           │
            ├───────────┼───────────┼───────────┼───────────┼───────────┤
 SAFE       │ 정상 URL  │ 정상 코드 │ 클린 HTML │ 추적기    │ 유효한    │
            │           │           │           │ 없음      │ 인증서    │
            └───────────┴───────────┴───────────┴───────────┴───────────┘
```

---

## 6. gRPC API 레퍼런스

### 6.1 `AnalyzeUrl` — URL 보안 분석

| 항목 | 설명 |
|------|------|
| **Request** | `AnalysisRequest` |
| **Response** | `AnalysisResponse` |
| **설명** | 주어진 URL의 피싱, 악성코드 위험을 분석합니다. |

```protobuf
// 요청
message AnalysisRequest {
    string url = 1;                    // 분석 대상 URL (필수)
    bool use_llm = 4;                  // LLM 심층 분석 사용 여부
    map<string, string> metadata = 5;  // 추가 메타데이터
}

// 응답
message AnalysisResponse {
    bool success = 1;                  // 분석 성공 여부
    string error_message = 2;          // 오류 메시지 (실패 시)
    ThreatReport threat_report = 3;    // 위협 보고서
    double analysis_time_ms = 4;       // 분석 소요 시간 (ms)
    bool cached = 5;                   // 캐시된 결과 여부
}
```

### 6.2 `AnalyzeScript` — JavaScript 코드 분석

| 항목 | 설명 |
|------|------|
| **Request** | `AnalysisRequest` |
| **Response** | `AnalysisResponse` |
| **설명** | 스크립트 코드의 악성 패턴을 분석합니다. |

```protobuf
// 요청 — script_code 필드 사용
message AnalysisRequest {
    string url = 1;                    // 스크립트 출처 URL (선택)
    string script_code = 3;            // JavaScript 코드 (필수)
    bool use_llm = 4;                  // LLM 사용 (10KB 이하만)
}
```

### 6.3 `AnalyzePage` — 웹 페이지 종합 분석

| 항목 | 설명 |
|------|------|
| **Request** | `AnalysisRequest` |
| **Response** | `PageSecurityReport` |
| **설명** | URL + HTML을 종합하여 보안 보고서를 생성합니다. |

```protobuf
// 요청 — 모든 필드 사용
message AnalysisRequest {
    string url = 1;                    // 페이지 URL (필수)
    string html_content = 2;           // HTML 소스 코드 (필수)
    string script_code = 3;            // 인라인 스크립트 (선택)
    bool use_llm = 4;                  // LLM 사용 여부
}

// 응답
message PageSecurityReport {
    string url = 1;
    string title = 2;
    ThreatReport threat_report = 4;
    SecurityScore security_score = 5;
    PhishingDetail phishing_detail = 6;
    MalwareDetail malware_detail = 7;
    PrivacyDetail privacy_detail = 8;
    repeated string blocked_urls = 9;
    double analysis_time_ms = 10;
}
```

### 6.4 `GetSecurityReport` — 집계 보고서 조회

| 항목 | 설명 |
|------|------|
| **Request** | `SecurityReportRequest` |
| **Response** | `SecurityReportResponse` |
| **설명** | 최근 분석 결과를 집계하여 반환합니다. |

```protobuf
message SecurityReportRequest {
    int32 limit = 1;                   // 최대 결과 수
    ThreatLevel min_level = 2;         // 최소 위협 수준 필터
    double since_timestamp = 3;        // 이후 시점 (Unix epoch)
}

message SecurityReportResponse {
    repeated ThreatReport reports = 1;
    int32 total_analyzed = 2;
    int32 safe_count = 3;
    int32 threat_count = 4;
    int32 cache_size = 5;
}
```

### 6.5 `StreamThreats` — 실시간 위협 스트리밍

| 항목 | 설명 |
|------|------|
| **Request** | `StreamRequest` |
| **Response** | `stream ThreatReport` |
| **설명** | 새 위협이 탐지될 때마다 클라이언트에 전송합니다. |

```protobuf
message StreamRequest {
    ThreatLevel min_level = 1;         // 최소 위협 수준
    repeated ThreatType threat_types = 2; // 관심 위협 유형 필터
}
// 서버 → 클라이언트 스트리밍
```

### 6.6 `HealthCheck` — 헬스 체크

| 항목 | 설명 |
|------|------|
| **Request** | `HealthCheckRequest` |
| **Response** | `HealthCheckResponse` |
| **설명** | 서버 상태를 확인합니다. |

```protobuf
message HealthCheckResponse {
    ServingStatus status = 1;          // SERVING / NOT_SERVING
    bool agent_initialized = 2;        // 에이전트 초기화 상태
    int32 cache_size = 3;              // 캐시 크기
    int32 total_reports = 4;           // 총 보고서 수
    int32 active_streams = 5;          // 활성 스트림 수
    string version = 6;                // 에이전트 버전
    double timestamp = 7;              // 서버 시각
}
```

---

## 7. 설정 가이드

### 7.1 전체 설정 구조

```python
AgentConfig
├── agent_name: str = "ordinal-security-agent"
├── version: str = "0.1.0"
├── debug: bool = False
├── log_level: str = "INFO"  # DEBUG|INFO|WARNING|ERROR|CRITICAL
├── data_dir: str = "data"
│
├── llm: LLMConfig
│   ├── api_key: str                    # OPENAI_API_KEY
│   ├── model_name: str = "gpt-4"
│   ├── temperature: float = 0.1        # 0.0~2.0
│   ├── max_tokens: int = 2048          # 1~8192
│   ├── use_local_fallback: bool = True
│   ├── local_model_name: str = "microsoft/DialoGPT-medium"
│   ├── cache_enabled: bool = True
│   ├── cache_ttl_seconds: int = 3600
│   ├── cache_max_size: int = 1000
│   ├── max_retries: int = 3
│   └── retry_base_delay: float = 1.0
│
├── threats: ThreatConfig
│   ├── phishing_threshold: float = 0.7
│   ├── malware_threshold: float = 0.6
│   ├── xss_threshold: float = 0.5
│   ├── privacy_threshold: float = 0.6
│   ├── overall_danger_threshold: float = 0.75
│   └── safe_score_minimum: int = 70
│
├── external_api: ExternalAPIConfig
│   ├── google_safe_browsing_key: str
│   ├── virustotal_key: str
│   ├── rate_limit_per_minute: int = 30
│   └── request_timeout: float = 10.0
│
├── database: DatabaseConfig
│   ├── db_path: str = "data/threats.db"
│   ├── blocklist_path: str = "data/blocklists"
│   └── embedding_cache_path: str = "data/embeddings_cache"
│
└── server: ServerConfig
    ├── host: str = "localhost"
    ├── port: int = 50051               # 1024~65535
    ├── max_workers: int = 4            # 1~32
    └── max_message_size: int = 10MB
```

### 7.2 환경 변수 매핑

모든 설정은 환경 변수로 오버라이드 가능합니다:

| 환경 변수 | 설정 값 | 기본값 |
|-----------|---------|--------|
| `OPENAI_API_KEY` | `llm.api_key` | `""` |
| `LLM_MODEL_NAME` | `llm.model_name` | `"gpt-4"` |
| `LLM_TEMPERATURE` | `llm.temperature` | `0.1` |
| `LLM_MAX_TOKENS` | `llm.max_tokens` | `2048` |
| `LLM_CACHE_ENABLED` | `llm.cache_enabled` | `True` |
| `LLM_CACHE_TTL_SECONDS` | `llm.cache_ttl_seconds` | `3600` |
| `THREAT_PHISHING_THRESHOLD` | `threats.phishing_threshold` | `0.7` |
| `THREAT_MALWARE_THRESHOLD` | `threats.malware_threshold` | `0.6` |
| `THREAT_PRIVACY_THRESHOLD` | `threats.privacy_threshold` | `0.6` |
| `API_GOOGLE_SAFE_BROWSING_KEY` | `external_api.google_safe_browsing_key` | `""` |
| `API_VIRUSTOTAL_KEY` | `external_api.virustotal_key` | `""` |
| `GRPC_HOST` | `server.host` | `"localhost"` |
| `GRPC_PORT` | `server.port` | `50051` |
| `GRPC_MAX_WORKERS` | `server.max_workers` | `4` |
| `AGENT_DEBUG` | `debug` | `False` |
| `AGENT_LOG_LEVEL` | `log_level` | `"INFO"` |

### 7.3 `.env` 파일 예시

```bash
# LLM 설정
OPENAI_API_KEY=sk-proj-...
LLM_MODEL_NAME=gpt-4
LLM_TEMPERATURE=0.1
LLM_CACHE_TTL_SECONDS=3600

# 위협 임계값
THREAT_PHISHING_THRESHOLD=0.7
THREAT_MALWARE_THRESHOLD=0.6
THREAT_PRIVACY_THRESHOLD=0.6

# 외부 API
API_GOOGLE_SAFE_BROWSING_KEY=AIza...
API_VIRUSTOTAL_KEY=...

# gRPC 서버
GRPC_HOST=localhost
GRPC_PORT=50051
GRPC_MAX_WORKERS=4

# 에이전트
AGENT_DEBUG=false
AGENT_LOG_LEVEL=INFO
```

---

## 8. 배포 가이드

### 8.1 Python 환경 설정

```bash
# Python 3.11+ 필요
cd ordinal-browser/agent

# 가상 환경 생성
python3 -m venv .venv
source .venv/bin/activate

# 의존성 설치
pip install -r requirements.txt

# gRPC proto 컴파일
python -m grpc_tools.protoc \
    --proto_path=proto \
    --python_out=proto \
    --grpc_python_out=proto \
    proto/security.proto
```

### 8.2 에이전트 실행

```bash
# 환경 변수 설정
export OPENAI_API_KEY=sk-proj-...

# gRPC 서버 시작
python -m agent.api.grpc_server

# 또는 설정 파일 사용
cp .env.example .env
# .env 편집 후
python -m agent.api.grpc_server
```

### 8.3 Docker 배포 (선택)

```dockerfile
FROM python:3.11-slim

WORKDIR /app
COPY agent/ ./agent/
COPY agent/requirements.txt .

RUN pip install --no-cache-dir -r requirements.txt

# gRPC proto 컴파일
RUN python -m grpc_tools.protoc \
    --proto_path=agent/proto \
    --python_out=agent/proto \
    --grpc_python_out=agent/proto \
    agent/proto/security.proto

EXPOSE 50051

CMD ["python", "-m", "agent.api.grpc_server"]
```

### 8.4 C++ 브라우저와 연동

1. Python Agent를 먼저 시작 (`localhost:50051`)
2. C++ 브라우저 빌드 시 `gRPC_FOUND=ON` 확인
3. `SecurityAgentConfig.grpc_server_address`를 Agent 주소로 설정
4. `SecurityAgentConfig.enable_llm_analysis = true` 활성화

---

## 부록: 분석기별 탐지 패턴 요약

### PhishingAnalyzer 탐지 패턴

| 카테고리 | 패턴 | 가중치 |
|---------|------|--------|
| IP 주소 URL | `192.168.x.x` 형태 | +0.30 |
| 호모글리프 | 키릴/그리스/전각 유사 문자 | +0.30 |
| 타이포스쿼팅 | 유명 도메인 레벤슈타인 거리 ≤ 20% | +0.25~0.35 |
| @ 기호 | URL에 `@` 포함 | +0.20 |
| 의심 TLD | `.tk`, `.ml`, `.xyz` 등 | +0.15 |
| 비정상 포트 | 80/443 외 포트 사용 | +0.15 |
| 브랜드명 도용 | 도메인에 `google`, `paypal` 등 포함 | +0.15 |
| 로그인 폼 | `<form>` + `action=login` | +0.15 |
| 파비콘 불일치 | 외부 도메인 파비콘 | +0.20 |
| 자동 폼 제출 | `onload=submit()` | +0.20 |

### MalwareAnalyzer 탐지 패턴

| 카테고리 | 패턴 | 가중치 |
|---------|------|--------|
| 크립토마이닝 | `coinhive`, `CryptoNight`, `miner.start` | +0.35 |
| 키로거 | `keydown` + `XMLHttpRequest` | +0.35 |
| 데이터 유출 | `document.cookie` + `fetch` | +0.30 |
| eval 체인 | `eval(atob(...))` | +0.25 (base64+실행) |
| iframe 주입 | `createElement('iframe')` | +0.20 |
| 난독화 | `_0x` 변수 + 높은 코드 밀도 + 높은 엔트로피 | 가중 합 |

### PrivacyAnalyzer 탐지 패턴

| 카테고리 | 패턴 수 | 심각도 |
|---------|--------|--------|
| 추적기 도메인 | 60+ 패턴 (Google, Facebook, 한국 등) | 0.25 |
| Canvas 핑거프린팅 | `toDataURL`, `getImageData` | 0.70 |
| WebGL 핑거프린팅 | `WEBGL_debug_renderer_info` | 0.80 |
| AudioContext | `createOscillator` + `getFloatFrequencyData` | 0.80 |
| WebRTC IP 유출 | `RTCPeerConnection` + `onicecandidate` | 0.90 |
| 픽셀 추적기 | 1x1 이미지, `sendBeacon`, `new Image()` | 0.10 |
