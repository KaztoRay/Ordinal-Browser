// V8 스텁 헤더 — CI 빌드 및 패키징용
// 실제 V8 라이브러리는 런타임에 필요합니다
#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace v8 {

class Isolate;
class Context;
class Value;
class String;
class Object;
class Array;
class Function;
class Script;
class Message;
class StackTrace;
class TryCatch;
class Platform;
class ArrayBuffer;
class External;
class FunctionTemplate;
class ObjectTemplate;
class Boolean;
class Number;
class Integer;
class Int32;
class Uint32;

template <class T> class Local {
public:
    Local() : val_(nullptr) {}
    Local(T* val) : val_(val) {}
    T* operator->() const { return val_; }
    T* operator*() const { return val_; }
    bool IsEmpty() const { return val_ == nullptr; }
    template <class S> Local<S> As() const { return Local<S>(); }
    static Local<T> New(Isolate* isolate, Local<T> other) { return other; }
private:
    T* val_;
};

template <class T> class Global {
public:
    Global() {}
    Global(Isolate* isolate, Local<T> that) {}
    void Reset() {}
    void Reset(Isolate* isolate, Local<T> other) {}
    bool IsEmpty() const { return true; }
    Local<T> Get(Isolate* isolate) const { return Local<T>(); }
};

template <class T> class MaybeLocal {
public:
    MaybeLocal() {}
    MaybeLocal(Local<T> val) : val_(val) {}
    bool IsEmpty() const { return val_.IsEmpty(); }
    bool ToLocal(Local<T>* out) const { *out = val_; return !val_.IsEmpty(); }
    Local<T> ToLocalChecked() const { return val_; }
private:
    Local<T> val_;
};

template <class T> class Maybe {
public:
    Maybe() : has_value_(false), value_() {}
    Maybe(T value) : has_value_(true), value_(value) {}
    bool IsNothing() const { return !has_value_; }
    bool IsJust() const { return has_value_; }
    T FromJust() const { return value_; }
    T FromMaybe(const T& default_value) const { return has_value_ ? value_ : default_value; }
private:
    bool has_value_;
    T value_;
};

class HandleScope {
public:
    HandleScope(Isolate* isolate) {}
    ~HandleScope() {}
};

class EscapableHandleScope : public HandleScope {
public:
    EscapableHandleScope(Isolate* isolate) : HandleScope(isolate) {}
    template <class T> Local<T> Escape(Local<T> value) { return value; }
};

class SealHandleScope {
public:
    SealHandleScope(Isolate* isolate) {}
    ~SealHandleScope() {}
};

enum class NewStringType { kNormal, kInternalized };

class String {
public:
    class Utf8Value {
    public:
        Utf8Value(Isolate* isolate, Local<Value> val) {}
        const char* operator*() const { return ""; }
        int length() const { return 0; }
    };
    static MaybeLocal<String> NewFromUtf8(Isolate* isolate, const char* data,
        NewStringType type = NewStringType::kNormal, int length = -1) {
        return MaybeLocal<String>();
    }
    int Length() const { return 0; }
    int Utf8Length(Isolate* isolate) const { return 0; }
};

class Value {
public:
    bool IsUndefined() const { return true; }
    bool IsNull() const { return false; }
    bool IsString() const { return false; }
    bool IsObject() const { return false; }
    bool IsArray() const { return false; }
    bool IsFunction() const { return false; }
    bool IsBoolean() const { return false; }
    bool IsNumber() const { return false; }
    bool IsInt32() const { return false; }
    bool IsUint32() const { return false; }
    bool IsTrue() const { return false; }
    bool IsFalse() const { return false; }
    MaybeLocal<String> ToString(Local<Context> context) const { return MaybeLocal<String>(); }
    Maybe<double> NumberValue(Local<Context> context) const { return Maybe<double>(); }
    Maybe<bool> BooleanValue(Isolate* isolate) const { return Maybe<bool>(); }
    Maybe<int32_t> Int32Value(Local<Context> context) const { return Maybe<int32_t>(); }
};

class Object : public Value {
public:
    static Local<Object> New(Isolate* isolate) { return Local<Object>(); }
    MaybeLocal<Value> Get(Local<Context> context, Local<Value> key) { return MaybeLocal<Value>(); }
    Maybe<bool> Set(Local<Context> context, Local<Value> key, Local<Value> value) { return Maybe<bool>(); }
};

class Array : public Object {
public:
    static Local<Array> New(Isolate* isolate, int length = 0) { return Local<Array>(); }
    uint32_t Length() const { return 0; }
};

class Boolean : public Value {
public:
    static Local<Boolean> New(Isolate* isolate, bool value) { return Local<Boolean>(); }
};

class Number : public Value {
public:
    static Local<Number> New(Isolate* isolate, double value) { return Local<Number>(); }
    double Value() const { return 0.0; }
};

class Integer : public Number {
public:
    static Local<Integer> New(Isolate* isolate, int32_t value) { return Local<Integer>(); }
    static Local<Integer> NewFromUnsigned(Isolate* isolate, uint32_t value) { return Local<Integer>(); }
};

struct FunctionCallbackInfo_Base {};
template <class T> class FunctionCallbackInfo : public FunctionCallbackInfo_Base {
public:
    int Length() const { return 0; }
    Local<Value> operator[](int i) const { return Local<Value>(); }
    Isolate* GetIsolate() const { return nullptr; }
    Local<Context> GetIsolateContext() const { return Local<Context>(); }
    void GetReturnValue() const {}
};

template <class T> class ReturnValue {
public:
    void Set(Local<T> value) {}
    void Set(double i) {}
    void Set(int32_t i) {}
    void Set(bool value) {}
    void SetUndefined() {}
    void SetNull() {}
};

using FunctionCallback = void (*)(const FunctionCallbackInfo<Value>&);

class FunctionTemplate {
public:
    static Local<FunctionTemplate> New(Isolate* isolate, FunctionCallback callback = nullptr) {
        return Local<FunctionTemplate>();
    }
    Local<Function> GetFunction(Local<Context> context) { return Local<Function>(); }
};

class ObjectTemplate {
public:
    static Local<ObjectTemplate> New(Isolate* isolate) { return Local<ObjectTemplate>(); }
    void Set(Isolate* isolate, const char* name, Local<FunctionTemplate> val) {}
    void Set(Local<String> name, Local<FunctionTemplate> val) {}
};

class Function : public Object {
public:
    MaybeLocal<Value> Call(Local<Context> context, Local<Value> recv, int argc, Local<Value> argv[]) {
        return MaybeLocal<Value>();
    }
};

class Script {
public:
    static MaybeLocal<Script> Compile(Local<Context> context, Local<String> source) {
        return MaybeLocal<Script>();
    }
    MaybeLocal<Value> Run(Local<Context> context) { return MaybeLocal<Value>(); }
};

class External : public Value {
public:
    static Local<External> New(Isolate* isolate, void* value) { return Local<External>(); }
    void* Value() const { return nullptr; }
};

class ArrayBuffer : public Object {
public:
    class Allocator {
    public:
        virtual ~Allocator() = default;
        virtual void* Allocate(size_t length) = 0;
        virtual void* AllocateUninitialized(size_t length) = 0;
        virtual void Free(void* data, size_t length) = 0;
        static Allocator* NewDefaultAllocator() { return nullptr; }
    };
};

class Message {
public:
    Local<String> Get() const { return Local<String>(); }
    int GetLineNumber(Local<Context> context) const { return 0; }
    int GetStartColumn() const { return 0; }
    MaybeLocal<String> GetSourceLine(Local<Context> context) const { return MaybeLocal<String>(); }
};

class TryCatch {
public:
    TryCatch(Isolate* isolate) {}
    ~TryCatch() {}
    bool HasCaught() const { return false; }
    Local<Value> Exception() const { return Local<Value>(); }
    Local<Message> Message_() const { return Local<Message>(); }
    void Reset() {}
};

class StackTrace {
public:
    static Local<StackTrace> CurrentStackTrace(Isolate* isolate, int frame_limit) {
        return Local<StackTrace>();
    }
};

struct ResourceConstraints {
    void set_max_old_generation_size_in_bytes(size_t) {}
    void set_max_young_generation_size_in_bytes(size_t) {}
};

struct CreateParams {
    ArrayBuffer::Allocator* array_buffer_allocator = nullptr;
    ResourceConstraints constraints;
};

class Isolate {
public:
    class Scope {
    public:
        Scope(Isolate* isolate) {}
        ~Scope() {}
    };
    static Isolate* New(const CreateParams& params) { return new Isolate(); }
    void Dispose() { delete this; }
    void Enter() {}
    void Exit() {}
    Local<Context> GetCurrentContext() { return Local<Context>(); }
    void TerminateExecution() {}
    bool IsExecutionTerminating() { return false; }
    void SetData(uint32_t slot, void* data) {}
    void* GetData(uint32_t slot) { return nullptr; }
};

class Context {
public:
    class Scope {
    public:
        Scope(Local<Context> context) {}
        ~Scope() {}
    };
    static Local<Context> New(Isolate* isolate, nullptr_t ext = nullptr,
                              MaybeLocal<ObjectTemplate> templ = MaybeLocal<ObjectTemplate>()) {
        return Local<Context>();
    }
    Isolate* GetIsolate() { return nullptr; }
    Local<Object> Global() { return Local<Object>(); }
};

class Platform {
public:
    virtual ~Platform() = default;
};

class V8 {
public:
    static void InitializeICU() {}
    static void InitializeICUDefaultLocation(const char* exec_path) {}
    static void InitializeExternalStartupData(const char* directory_path) {}
    static void InitializeExternalStartupDataFromFile(const char* snapshot_blob) {}
    static void InitializePlatform(Platform* platform) {}
    static void DisposePlatform() {}
    static bool Initialize() { return true; }
    static void ShutdownPlatform() {}
    static bool Dispose() { return true; }
    static void SetFlagsFromString(const char* str) {}
    static void SetFlagsFromString(const char* str, size_t length) {}
};

} // namespace v8
