# V8 JavaScript Engine 통합 가이드

## 1. macOS ARM64 빌드 방법

### 1.1 사전 요구 사항

```bash
# Xcode Command Line Tools
xcode-select --install

# Homebrew 패키지
brew install python3 git
```

### 1.2 depot_tools 설치

```bash
# depot_tools 클론 (V8 빌드에 필요한 Google 도구 모음)
cd ~
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git

# PATH에 추가 (~/.zshrc 또는 ~/.bashrc)
export PATH="$HOME/depot_tools:$PATH"

# 적용
source ~/.zshrc
```

### 1.3 V8 소스 코드 가져오기

```bash
# 작업 디렉토리 생성
mkdir -p ~/v8-build && cd ~/v8-build

# V8 소스 가져오기 (fetch가 gclient를 자동 설정)
fetch v8

# V8 디렉토리로 이동
cd v8

# 안정 버전 체크아웃 (예: 12.8)
git checkout branch-heads/12.8

# 의존성 동기화
gclient sync
```

### 1.4 빌드 설정 (ARM64)

```bash
# GN 빌드 설정 생성 (ARM64 릴리스)
tools/dev/v8gen.py arm64.release

# 빌드 옵션 편집
cat > out.gn/arm64.release/args.gn << 'EOF'
# Ordinal Browser V8 빌드 설정 — macOS ARM64

# 릴리스 빌드
is_debug = false
is_component_build = false

# ARM64 대상
target_cpu = "arm64"
v8_target_cpu = "arm64"

# 단일 라이브러리로 빌드 (v8_monolith)
v8_monolithic = true
v8_use_external_startup_data = false

# 보안 기능 활성화
v8_enable_sandbox = true
v8_enable_pointer_compression = true

# ICU 내장 (국제화 지원)
v8_enable_i18n_support = true

# WebAssembly 비활성화 (필요 없는 경우)
v8_enable_webassembly = false

# 디버그 심볼 (선택적)
symbol_level = 1
EOF
```

### 1.5 빌드 실행

```bash
# 빌드 (멀티코어 사용)
ninja -C out.gn/arm64.release v8_monolith

# 결과물 확인
# - out.gn/arm64.release/obj/libv8_monolith.a  (~80MB)
```

### 1.6 Ordinal Browser에 V8 연동

```bash
# V8을 프로젝트에 복사
cd ~/Desktop/ordinal-browser
mkdir -p third_party/v8

# 헤더 복사
cp -r ~/v8-build/v8/include third_party/v8/

# 빌드 산출물 복사
mkdir -p third_party/v8/out.gn/arm64.release/obj
cp ~/v8-build/v8/out.gn/arm64.release/obj/libv8_monolith.a \
   third_party/v8/out.gn/arm64.release/obj/

# CMake 빌드 (V8 자동 감지)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)
```

---

## 2. V8 API 개요

### 2.1 핵심 개념

V8 API의 계층 구조는 다음과 같습니다:

```
v8::Platform          (전역, 프로세스당 1개)
    │
    └── v8::Isolate   (독립 실행 환경, 탭당 1개)
            │
            ├── v8::HandleScope     (핸들 수명 관리)
            │
            └── v8::Context         (JavaScript 전역 객체)
                    │
                    ├── v8::Script          (컴파일된 스크립트)
                    ├── v8::FunctionTemplate (C++→JS 함수 바인딩)
                    └── v8::ObjectTemplate   (C++→JS 객체 바인딩)
```

### 2.2 v8::Platform

V8 엔진의 전역 플랫폼 인스턴스입니다. 프로세스당 한 번만 초기화합니다.

```cpp
#include <v8.h>
#include <libplatform/libplatform.h>

// 전역 초기화 (main 시작 시)
std::unique_ptr<v8::Platform> platform =
    v8::platform::NewDefaultPlatform();
v8::V8::InitializePlatform(platform.get());
v8::V8::Initialize();

// 전역 종료 (main 종료 시)
v8::V8::Dispose();
v8::V8::DisposePlatform();
```

### 2.3 v8::Isolate

V8 실행 환경의 독립된 인스턴스입니다. 각 Isolate는 자체 힙과 GC를 가집니다.
**Isolate는 스레드 안전하지 않으므로** 한 스레드에서만 접근해야 합니다.

```cpp
// Isolate 생성 파라미터
v8::Isolate::CreateParams create_params;
create_params.array_buffer_allocator =
    v8::ArrayBuffer::Allocator::NewDefaultAllocator();

// 메모리 제한 설정
v8::ResourceConstraints constraints;
constraints.set_max_old_generation_size_in_bytes(256 * 1024 * 1024);  // 256MB
constraints.set_max_young_generation_size_in_bytes(16 * 1024 * 1024); // 16MB
create_params.constraints = constraints;

// Isolate 생성
v8::Isolate* isolate = v8::Isolate::New(create_params);

// ... 사용 ...

// Isolate 해제
isolate->Dispose();
delete create_params.array_buffer_allocator;
```

### 2.4 v8::Context

JavaScript 전역 스코프를 나타냅니다. 하나의 Isolate에서 여러 Context를 만들 수 있습니다.

```cpp
// Isolate 스코프 진입
v8::Isolate::Scope isolate_scope(isolate);

// HandleScope 생성 (로컬 핸들 수명 관리)
v8::HandleScope handle_scope(isolate);

// Context 생성
v8::Local<v8::Context> context = v8::Context::New(isolate);

// Context 스코프 진입
v8::Context::Scope context_scope(context);

// 이제 JavaScript 코드를 실행할 수 있습니다
```

### 2.5 핸들 시스템

V8은 GC가 객체를 이동시킬 수 있으므로, 핸들을 통해 간접 참조합니다.

| 핸들 타입 | 수명 | 용도 |
|-----------|------|------|
| `v8::Local<T>` | HandleScope 내 | 함수 내 임시 참조 (가장 일반적) |
| `v8::Global<T>` | 명시적 해제까지 | 장기 참조 (전역 변수, 콜백 등) |
| `v8::Persistent<T>` | 명시적 해제까지 | Global의 레거시 버전 |
| `v8::Eternal<T>` | 프로세스 종료까지 | 절대 GC되지 않는 참조 |
| `v8::MaybeLocal<T>` | HandleScope 내 | 실패 가능한 연산의 결과 |

```cpp
// Local 핸들 — HandleScope 안에서만 유효
{
    v8::HandleScope scope(isolate);
    v8::Local<v8::String> str =
        v8::String::NewFromUtf8(isolate, "hello").ToLocalChecked();
    // scope 종료 시 str 자동 해제
}

// Global 핸들 — 명시적 수명 관리
v8::Global<v8::Function> callback;
{
    v8::HandleScope scope(isolate);
    v8::Local<v8::Function> fn = /* ... */;
    callback.Reset(isolate, fn);  // Global로 승격
}
// callback은 Reset() 또는 소멸자에서 해제

// MaybeLocal — 실패 가능한 연산
v8::MaybeLocal<v8::Value> maybe_result = script->Run(context);
if (maybe_result.IsEmpty()) {
    // 실행 실패 (예외 발생)
} else {
    v8::Local<v8::Value> result = maybe_result.ToLocalChecked();
}
```

---

## 3. 스크립트 실행 흐름

### 3.1 전체 흐름

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ 소스 코드    │     │ 컴파일       │     │ 실행         │
│ (문자열)     │────→│ (Script)     │────→│ (Run)        │
└──────────────┘     └──────────────┘     └──────┬───────┘
                                                  │
                                           ┌──────▼───────┐
                                           │ 결과/예외    │
                                           └──────────────┘
```

### 3.2 코드 예시

```cpp
JsResult V8Engine::executeScript(
    const std::string& source_code,
    const std::string& source_name
) {
    JsResult result;
    auto start = std::chrono::high_resolution_clock::now();

    // 1. Isolate/HandleScope/Context 스코프 설정
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    v8::Local<v8::Context> context =
        v8::Local<v8::Context>::New(isolate_, context_);
    v8::Context::Scope context_scope(context);

    // 2. TryCatch 설정 (예외 포착)
    v8::TryCatch try_catch(isolate_);

    // 3. 소스 코드를 V8 문자열로 변환
    v8::Local<v8::String> source =
        v8::String::NewFromUtf8(
            isolate_, source_code.c_str()
        ).ToLocalChecked();

    // 4. ScriptOrigin 설정 (디버깅용 소스 이름)
    v8::Local<v8::String> name =
        v8::String::NewFromUtf8(
            isolate_, source_name.c_str()
        ).ToLocalChecked();
    v8::ScriptOrigin origin(name);

    // 5. 스크립트 컴파일
    v8::MaybeLocal<v8::Script> maybe_script =
        v8::Script::Compile(context, source, &origin);

    if (maybe_script.IsEmpty()) {
        // 컴파일 에러 (구문 오류)
        result.success = false;
        result.error_message = extractException(try_catch);
        return result;
    }

    v8::Local<v8::Script> script = maybe_script.ToLocalChecked();

    // 6. 스크립트 실행
    v8::MaybeLocal<v8::Value> maybe_result = script->Run(context);

    if (maybe_result.IsEmpty()) {
        // 실행 에러 (런타임 예외 또는 타임아웃)
        result.success = false;
        if (try_catch.HasTerminated()) {
            result.error_message = "스크립트 실행 시간 초과";
        } else {
            result.error_message = extractException(try_catch);
        }
        return result;
    }

    // 7. 결과 추출
    v8::Local<v8::Value> value = maybe_result.ToLocalChecked();
    v8::String::Utf8Value utf8(isolate_, value);
    result.success = true;
    result.value = *utf8 ? *utf8 : "";

    // 8. 실행 시간 측정
    auto end = std::chrono::high_resolution_clock::now();
    result.execution_time_ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}
```

### 3.3 구문 유효성 검사 (실행 없이)

```cpp
std::optional<std::string> V8Engine::validateScript(
    const std::string& source_code
) {
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    v8::Local<v8::Context> context =
        v8::Local<v8::Context>::New(isolate_, context_);
    v8::Context::Scope context_scope(context);
    v8::TryCatch try_catch(isolate_);

    v8::Local<v8::String> source =
        v8::String::NewFromUtf8(
            isolate_, source_code.c_str()
        ).ToLocalChecked();

    // 컴파일만 시도 (Run은 호출하지 않음)
    v8::MaybeLocal<v8::Script> maybe_script =
        v8::Script::Compile(context, source);

    if (maybe_script.IsEmpty()) {
        return extractException(try_catch);  // 구문 오류 메시지
    }

    return std::nullopt;  // 유효한 JavaScript
}
```

---

## 4. 보안 고려사항

### 4.1 V8 샌드박스

V8 엔진은 웹 페이지의 JavaScript를 실행하므로, 악성 코드로부터 호스트 시스템을 보호하는 것이 매우 중요합니다.

```
┌─────────────────────────────────────────────────────┐
│ 호스트 시스템                                        │
│                                                     │
│  ┌──────────────────────────────────────────────┐   │
│  │ V8 샌드박스                                   │   │
│  │                                              │   │
│  │  ┌────────────────────────────────────────┐  │   │
│  │  │ JavaScript 코드                        │  │   │
│  │  │                                        │  │   │
│  │  │  ✗ 파일 시스템 접근 불가               │  │   │
│  │  │  ✗ 네트워크 직접 접근 불가             │  │   │
│  │  │  ✗ 프로세스 생성 불가                  │  │   │
│  │  │  ✗ 네이티브 코드 실행 불가             │  │   │
│  │  │  ✓ 허용된 API만 사용 가능              │  │   │
│  │  └────────────────────────────────────────┘  │   │
│  │                                              │   │
│  │  ResourceConstraints:                         │   │
│  │  - max_old_generation: 256MB                  │   │
│  │  - max_young_generation: 16MB                 │   │
│  │  - 타임아웃: 5000ms                           │   │
│  └──────────────────────────────────────────────┘   │
│                                                     │
│  허용된 네이티브 API (FunctionTemplate):             │
│  - console.log()     → C++ 로깅                    │
│  - fetch() 래퍼      → RequestInterceptor 경유      │
│  - DOM API 래퍼      → DomTree 접근                  │
└─────────────────────────────────────────────────────┘
```

### 4.2 메모리 제한 (ResourceConstraints)

```cpp
v8::ResourceConstraints constraints;

// Old Generation: 장기 생존 객체의 최대 힙 크기
constraints.set_max_old_generation_size_in_bytes(
    256 * 1024 * 1024  // 256MB
);

// Young Generation: 새로 생성된 객체의 최대 힙 크기
constraints.set_max_young_generation_size_in_bytes(
    16 * 1024 * 1024   // 16MB
);

// Isolate 생성 시 적용
v8::Isolate::CreateParams params;
params.constraints = constraints;
v8::Isolate* isolate = v8::Isolate::New(params);
```

메모리 한도를 초과하면 V8이 OOM(Out of Memory) 에러를 발생시키고,
Ordinal Browser의 `V8Engine`이 이를 포착하여 안전하게 탭을 닫습니다.

### 4.3 실행 타임아웃 (TerminateExecution)

무한 루프나 과도한 계산을 방지하기 위해 별도 스레드에서 타임아웃을 감시합니다.

```cpp
// 타임아웃 감시 스레드
void V8Engine::startTimeoutWatchdog(uint32_t timeout_ms) {
    std::thread([this, timeout_ms]() {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(timeout_ms)
        );
        // 시간 초과 시 강제 종료
        isolate_->TerminateExecution();
    }).detach();
}

// 스크립트 실행
JsResult V8Engine::executeScript(const std::string& code, ...) {
    // 타임아웃 감시 시작 (5초)
    startTimeoutWatchdog(config_.script_timeout_ms);

    // ... 스크립트 실행 ...

    // TryCatch에서 종료 확인
    if (try_catch.HasTerminated()) {
        // CancelTerminateExecution() — 다음 실행을 위해 상태 리셋
        isolate_->CancelTerminateExecution();
        result.error_message = "스크립트 실행 시간 초과";
    }
}
```

### 4.4 V8_ENABLE_SANDBOX 컴파일 플래그

V8 12.0+에서 도입된 메모리 안전 샌드박스입니다. CMake에서 다음과 같이 활성화됩니다:

```cmake
target_compile_definitions(ordinal-browser PRIVATE
    V8_COMPRESS_POINTERS
    V8_ENABLE_SANDBOX
)
```

이 플래그는 V8의 내부 포인터를 압축하고, 메모리 접근을 격리하여
exploit을 통한 임의 코드 실행을 방지합니다.

---

## 5. 커스텀 API 바인딩

### 5.1 C++ 함수를 JavaScript에 노출

`FunctionTemplate`을 사용하여 C++ 함수를 JavaScript 전역 객체에 등록합니다.

```cpp
void V8Engine::registerNativeFunction(
    const std::string& name,
    NativeFunction func
) {
    // 네이티브 함수를 맵에 저장
    native_functions_[name] = std::move(func);

    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    v8::Local<v8::Context> context =
        v8::Local<v8::Context>::New(isolate_, context_);
    v8::Context::Scope context_scope(context);

    // FunctionTemplate 생성
    // External로 this 포인터를 전달하여 콜백에서 접근
    v8::Local<v8::External> data =
        v8::External::New(isolate_, this);

    v8::Local<v8::FunctionTemplate> fn_template =
        v8::FunctionTemplate::New(
            isolate_,
            NativeFunctionCallback,  // 정적 콜백 함수
            data                     // 콜백에서 사용할 데이터
        );

    // 전역 객체에 등록
    v8::Local<v8::Function> fn =
        fn_template->GetFunction(context).ToLocalChecked();
    v8::Local<v8::String> fn_name =
        v8::String::NewFromUtf8(
            isolate_, name.c_str()
        ).ToLocalChecked();

    context->Global()->Set(context, fn_name, fn).Check();
}

// 정적 콜백 — V8이 호출하면 NativeFunction을 찾아서 실행
static void NativeFunctionCallback(
    const v8::FunctionCallbackInfo<v8::Value>& info
) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::HandleScope scope(isolate);

    // this 포인터 복원
    V8Engine* engine = static_cast<V8Engine*>(
        v8::Local<v8::External>::Cast(info.Data())->Value()
    );

    // 인수를 std::vector<std::string>으로 변환
    std::vector<std::string> args;
    for (int i = 0; i < info.Length(); i++) {
        v8::String::Utf8Value utf8(isolate, info[i]);
        args.push_back(*utf8 ? *utf8 : "");
    }

    // 함수 이름 추출 (info.Callee()에서)
    // ... 매핑된 NativeFunction 호출 ...

    // 반환값 설정
    std::string result_str = native_func(args);
    info.GetReturnValue().Set(
        v8::String::NewFromUtf8(
            isolate, result_str.c_str()
        ).ToLocalChecked()
    );
}
```

### 5.2 사용 예시

```cpp
// console.log 바인딩
engine.registerNativeFunction("_log", [](const auto& args) {
    for (const auto& arg : args) {
        std::cout << arg << " ";
    }
    std::cout << std::endl;
    return "undefined";
});

// JavaScript에서 사용
engine.executeScript(R"(
    _log("Hello from JavaScript!");
    _log("1 + 2 =", 1 + 2);
)");
```

### 5.3 전역 변수 설정/조회

```cpp
void V8Engine::setGlobalVariable(
    const std::string& name,
    const std::string& json_value
) {
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    v8::Local<v8::Context> context =
        v8::Local<v8::Context>::New(isolate_, context_);
    v8::Context::Scope context_scope(context);

    // JSON 파싱하여 V8 값으로 변환
    v8::Local<v8::String> json_str =
        v8::String::NewFromUtf8(
            isolate_, json_value.c_str()
        ).ToLocalChecked();

    v8::Local<v8::Value> parsed =
        v8::JSON::Parse(context, json_str).ToLocalChecked();

    // 전역 객체에 속성 설정
    v8::Local<v8::String> prop_name =
        v8::String::NewFromUtf8(
            isolate_, name.c_str()
        ).ToLocalChecked();

    context->Global()->Set(context, prop_name, parsed).Check();
}

std::optional<std::string> V8Engine::getGlobalVariable(
    const std::string& name
) {
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);
    v8::Local<v8::Context> context =
        v8::Local<v8::Context>::New(isolate_, context_);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::String> prop_name =
        v8::String::NewFromUtf8(
            isolate_, name.c_str()
        ).ToLocalChecked();

    v8::MaybeLocal<v8::Value> maybe_value =
        context->Global()->Get(context, prop_name);

    if (maybe_value.IsEmpty()) return std::nullopt;

    v8::Local<v8::Value> value = maybe_value.ToLocalChecked();

    // V8 값을 JSON 문자열로 직렬화
    v8::Local<v8::String> json =
        v8::JSON::Stringify(context, value).ToLocalChecked();

    v8::String::Utf8Value utf8(isolate_, json);
    return *utf8 ? std::string(*utf8) : std::nullopt;
}
```

---

## 6. 에러 핸들링

### 6.1 TryCatch 패턴

V8의 예외 처리는 `v8::TryCatch`를 통해 수행됩니다.

```cpp
v8::TryCatch try_catch(isolate);

// 스크립트 실행
v8::MaybeLocal<v8::Value> result = script->Run(context);

if (try_catch.HasCaught()) {
    // 예외 발생
    v8::Local<v8::Value> exception = try_catch.Exception();
    v8::String::Utf8Value exception_str(isolate, exception);

    // 스택 트레이스 추출
    v8::Local<v8::Message> message = try_catch.Message();
    if (!message.IsEmpty()) {
        // 파일명
        v8::String::Utf8Value filename(
            isolate, message->GetScriptResourceName()
        );
        // 라인 번호
        int line = message->GetLineNumber(context).FromMaybe(0);
        // 컬럼 번호
        int column = message->GetStartColumn(context).FromMaybe(0);

        // 소스 라인
        v8::Local<v8::String> source_line =
            message->GetSourceLine(context).ToLocalChecked();
        v8::String::Utf8Value source_str(isolate, source_line);

        // 에러 메시지 포맷
        std::string error = fmt::format(
            "{}:{}:{}: {}\n  {}\n  {}^",
            *filename, line, column,
            *exception_str,
            *source_str,
            std::string(column, ' ')
        );
    }
}
```

### 6.2 에러 타입 분류

```cpp
JsErrorType classifyError(v8::Isolate* isolate, v8::TryCatch& try_catch) {
    if (try_catch.HasTerminated()) {
        return JsErrorType::TimeoutError;
    }

    v8::Local<v8::Value> exception = try_catch.Exception();

    if (exception->IsObject()) {
        v8::Local<v8::Object> obj = exception.As<v8::Object>();
        v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
        v8::Local<v8::String> name_key =
            v8::String::NewFromUtf8Literal(isolate, "name");

        v8::MaybeLocal<v8::Value> maybe_name =
            obj->Get(ctx, name_key);

        if (!maybe_name.IsEmpty()) {
            v8::String::Utf8Value name(
                isolate, maybe_name.ToLocalChecked()
            );

            std::string error_name = *name ? *name : "";
            if (error_name == "SyntaxError")    return JsErrorType::SyntaxError;
            if (error_name == "TypeError")      return JsErrorType::TypeError;
            if (error_name == "ReferenceError") return JsErrorType::ReferenceError;
            if (error_name == "RangeError")     return JsErrorType::RangeError;
        }
    }

    return JsErrorType::Unknown;
}
```

---

## 7. 메모리 관리

### 7.1 가비지 컬렉션 개요

V8의 GC는 세대별(Generational) 수집기를 사용합니다:

```
┌──────────────────────────────────────────────┐
│ V8 Heap                                      │
│                                              │
│  ┌────────────────┐  ┌────────────────────┐  │
│  │ Young Gen      │  │ Old Generation     │  │
│  │ (Scavenger GC) │  │ (Mark-Sweep-       │  │
│  │                │  │  Compact GC)       │  │
│  │ ┌────┐ ┌────┐ │  │                    │  │
│  │ │From│ │To  │ │  │ 장기 생존 객체     │  │
│  │ │ 영역│ │영역│ │  │ (여러 Scavenge를  │  │
│  │ │    │ │    │ │  │  살아남은 객체)     │  │
│  │ └────┘ └────┘ │  │                    │  │
│  │ 새 객체 할당   │  │                    │  │
│  │ (대부분 여기서 │  │                    │  │
│  │  빠르게 수거)  │  │                    │  │
│  └────────────────┘  └────────────────────┘  │
│                                              │
│  ┌────────────────────────────────────────┐  │
│  │ Large Object Space                     │  │
│  │ (1페이지 이상 큰 객체)                  │  │
│  └────────────────────────────────────────┘  │
└──────────────────────────────────────────────┘
```

- **Scavenge (Minor GC)**: Young Generation의 빠른 수집. From 영역에서 살아있는 객체를 To 영역으로 복사. 밀리초 단위.
- **Mark-Sweep-Compact (Major GC)**: Old Generation의 전체 수집. 마킹 → 스위핑 → 컴팩팅. 수십 밀리초.
- **Incremental Marking**: Major GC를 여러 작은 단계로 분할하여 긴 정지(pause)를 방지.

### 7.2 수동 GC 트리거

```cpp
void V8Engine::collectGarbage() {
    isolate_->LowMemoryNotification();
    // V8이 가능한 한 많은 메모리를 회수하도록 힌트
}

size_t V8Engine::getHeapUsedBytes() const {
    v8::HeapStatistics stats;
    isolate_->GetHeapStatistics(&stats);
    return stats.used_heap_size();
}
```

### 7.3 약한 참조 (Weak References)

GC가 객체를 수거할 때 알림을 받으려면 약한 콜백을 등록합니다.

```cpp
// Global 핸들에 약한 콜백 설정
v8::Global<v8::Object> weak_ref;
weak_ref.Reset(isolate, some_object);

// SetWeak — GC가 이 객체를 수거할 때 콜백 호출
weak_ref.SetWeak(
    &my_custom_data,
    [](const v8::WeakCallbackInfo<MyData>& info) {
        MyData* data = info.GetParameter();
        // C++ 측 정리 작업 수행
        delete data;
    },
    v8::WeakCallbackType::kParameter
);
```

### 7.4 컨텍스트 리셋

탭 이동이나 새 페이지 로드 시 기존 JavaScript 상태를 초기화합니다.

```cpp
void V8Engine::resetContext() {
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope handle_scope(isolate_);

    // 기존 컨텍스트 해제
    context_.Reset();

    // 새 컨텍스트 생성
    v8::Local<v8::Context> new_context =
        v8::Context::New(isolate_);
    context_.Reset(isolate_, new_context);

    // 네이티브 함수 재등록
    for (const auto& [name, func] : native_functions_) {
        registerNativeFunction(name, func);
    }
}
```

---

## 부록: Ordinal Browser의 V8Engine 클래스 설계

```
V8Engine
├── static s_platform_ (전역 Platform, 프로세스당 1개)
├── static s_initialized_ (전역 초기화 상태)
│
├── static initialize(config) → bool  // 전역 초기화
├── static shutdown()                 // 전역 종료
├── static isInitialized() → bool
│
├── Impl (PIMPL, V8 헤더 의존성 격리)
│   ├── isolate_ (v8::Isolate*)
│   ├── context_ (v8::Global<v8::Context>)
│   └── allocator_ (ArrayBuffer::Allocator)
│
├── executeScript(code, name) → JsResult       // 스크립트 실행
├── validateScript(code) → optional<string>     // 구문 검사
├── registerNativeFunction(name, func)          // API 바인딩
├── setGlobalVariable(name, json)               // 전역 변수 설정
├── getGlobalVariable(name) → optional<string>  // 전역 변수 조회
├── resetContext()                              // 컨텍스트 초기화
├── getHeapUsedBytes() → size_t                 // 메모리 사용량
├── collectGarbage()                            // GC 수동 실행
└── terminateExecution()                        // 강제 종료
```
