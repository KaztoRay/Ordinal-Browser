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
class Data;
class Name;
class Primitive;
class ScriptOrigin;

template <class T> class Local {
public:
    Local() : val_(nullptr) {}
    Local(T* val) : val_(val) {}
    T* operator->() const { return val_; }
    T* operator*() const { return val_; }
    bool IsEmpty() const { return val_ == nullptr; }
    template <class S> Local<S> As() const { return Local<S>(); }
    static Local<T> New(Isolate* isolate, Local<T> other) { return other; }
    template <class S> operator Local<S>() const { return Local<S>(); }
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
    operator Local<T>() const { return Local<T>(); }
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
    HandleScope(Isolate* isolate) { (void)isolate; }
    ~HandleScope() {}
};

class EscapableHandleScope : public HandleScope {
public:
    EscapableHandleScope(Isolate* isolate) : HandleScope(isolate) {}
    template <class T> Local<T> Escape(Local<T> value) { return value; }
};

class SealHandleScope {
public:
    SealHandleScope(Isolate* isolate) { (void)isolate; }
    ~SealHandleScope() {}
};

enum class NewStringType { kNormal, kInternalized };

class Data {
public:
    virtual ~Data() = default;
};

class Value : public Data {
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

class Primitive : public Value {};
class Name : public Primitive {};

class String : public Name {
public:
    class Utf8Value {
    public:
        Utf8Value(Isolate* isolate, Local<Value> val) { (void)isolate; (void)val; }
        Utf8Value(Isolate* isolate, Local<String> val) { (void)isolate; (void)val; }
        const char* operator*() const { return ""; }
        int length() const { return 0; }
    };
    static MaybeLocal<String> NewFromUtf8(Isolate* isolate, const char* data,
        NewStringType type = NewStringType::kNormal, int length = -1) {
        (void)isolate; (void)data; (void)type; (void)length;
        return MaybeLocal<String>();
    }
    int Length() const { return 0; }
    int Utf8Length(Isolate* isolate) const { (void)isolate; return 0; }
};

class Object : public Value {
public:
    static Local<Object> New(Isolate* isolate) { (void)isolate; return Local<Object>(); }
    MaybeLocal<Value> Get(Local<Context> context, Local<Value> key) {
        (void)context; (void)key; return MaybeLocal<Value>();
    }
    MaybeLocal<Value> Get(Local<Context> context, uint32_t index) {
        (void)context; (void)index; return MaybeLocal<Value>();
    }
    Maybe<bool> Set(Local<Context> context, Local<Value> key, Local<Value> value) {
        (void)context; (void)key; (void)value; return Maybe<bool>();
    }
    Maybe<bool> Set(Local<Context> context, uint32_t index, Local<Value> value) {
        (void)context; (void)index; (void)value; return Maybe<bool>();
    }
    Maybe<bool> Has(Local<Context> context, Local<Value> key) {
        (void)context; (void)key; return Maybe<bool>();
    }
    MaybeLocal<Array> GetPropertyNames(Local<Context> context) {
        (void)context; return MaybeLocal<Array>();
    }
    Local<Value> GetPrototype() { return Local<Value>(); }
    void SetInternalField(int index, Local<Value> value) { (void)index; (void)value; }
    Local<Value> GetInternalField(int index) { (void)index; return Local<Value>(); }
    int InternalFieldCount() const { return 0; }
};

class Array : public Object {
public:
    static Local<Array> New(Isolate* isolate, int length = 0) { (void)isolate; (void)length; return Local<Array>(); }
    uint32_t Length() const { return 0; }
};

class Boolean : public Primitive {
public:
    static Local<Boolean> New(Isolate* isolate, bool value) { (void)isolate; (void)value; return Local<Boolean>(); }
};

class Number : public Primitive {
public:
    static Local<Number> New(Isolate* isolate, double value) { (void)isolate; (void)value; return Local<Number>(); }
    double Value() const { return 0.0; }
};

class Integer : public Number {
public:
    static Local<Integer> New(Isolate* isolate, int32_t value) { (void)isolate; (void)value; return Local<Integer>(); }
    static Local<Integer> NewFromUnsigned(Isolate* isolate, uint32_t value) { (void)isolate; (void)value; return Local<Integer>(); }
    int64_t Value() const { return 0; }
};

class Int32 : public Integer {
public:
    int32_t Value() const { return 0; }
};

class Uint32 : public Integer {
public:
    uint32_t Value() const { return 0; }
};

struct FunctionCallbackInfo_Base {};
template <class T> class FunctionCallbackInfo : public FunctionCallbackInfo_Base {
public:
    int Length() const { return 0; }
    Local<Value> operator[](int i) const { (void)i; return Local<Value>(); }
    Isolate* GetIsolate() const { return nullptr; }
    Local<Object> This() const { return Local<Object>(); }
    Local<Object> Holder() const { return Local<Object>(); }
    ReturnValue<T> GetReturnValue() const { return ReturnValue<T>(); }
};

template <class T> class ReturnValue {
public:
    void Set(Local<T> value) { (void)value; }
    void Set(double i) { (void)i; }
    void Set(int32_t i) { (void)i; }
    void Set(uint32_t i) { (void)i; }
    void Set(bool value) { (void)value; }
    void SetUndefined() {}
    void SetNull() {}
    void SetEmptyString() {}
};

using FunctionCallback = void (*)(const FunctionCallbackInfo<Value>&);

class ScriptOrigin {
public:
    ScriptOrigin(Isolate* isolate, Local<Value> resource_name,
                 int line_offset = 0, int column_offset = 0) {
        (void)isolate; (void)resource_name; (void)line_offset; (void)column_offset;
    }
    ScriptOrigin(Local<Value> resource_name,
                 int line_offset = 0, int column_offset = 0) {
        (void)resource_name; (void)line_offset; (void)column_offset;
    }
};

class FunctionTemplate {
public:
    static Local<FunctionTemplate> New(Isolate* isolate, FunctionCallback callback = nullptr,
                                        Local<Value> data = Local<Value>()) {
        (void)isolate; (void)callback; (void)data;
        return Local<FunctionTemplate>();
    }
    Local<Function> GetFunction(Local<Context> context) { (void)context; return Local<Function>(); }
    Local<ObjectTemplate> InstanceTemplate() { return Local<ObjectTemplate>(); }
    Local<ObjectTemplate> PrototypeTemplate() { return Local<ObjectTemplate>(); }
    void SetClassName(Local<String> name) { (void)name; }
};

class ObjectTemplate {
public:
    static Local<ObjectTemplate> New(Isolate* isolate, Local<FunctionTemplate> constructor = Local<FunctionTemplate>()) {
        (void)isolate; (void)constructor;
        return Local<ObjectTemplate>();
    }
    void Set(Isolate* isolate, const char* name, Local<Data> val) { (void)isolate; (void)name; (void)val; }
    void Set(Local<Name> name, Local<Data> val) { (void)name; (void)val; }
    void SetInternalFieldCount(int count) { (void)count; }
};

class Function : public Object {
public:
    static MaybeLocal<Function> New(Local<Context> context, FunctionCallback callback,
                                     Local<Value> data = Local<Value>()) {
        (void)context; (void)callback; (void)data;
        return MaybeLocal<Function>();
    }
    MaybeLocal<Value> Call(Local<Context> context, Local<Value> recv, int argc, Local<Value> argv[]) {
        (void)context; (void)recv; (void)argc; (void)argv;
        return MaybeLocal<Value>();
    }
    Local<Value> GetName() const { return Local<Value>(); }
};

class Script {
public:
    static MaybeLocal<Script> Compile(Local<Context> context, Local<String> source,
                                       ScriptOrigin* origin = nullptr) {
        (void)context; (void)source; (void)origin;
        return MaybeLocal<Script>();
    }
    MaybeLocal<Value> Run(Local<Context> context) { (void)context; return MaybeLocal<Value>(); }
};

class External : public Value {
public:
    static Local<External> New(Isolate* isolate, void* value) { (void)isolate; (void)value; return Local<External>(); }
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
    size_t ByteLength() const { return 0; }
};

class Message {
public:
    Local<String> Get() const { return Local<String>(); }
    Maybe<int> GetLineNumber(Local<Context> context) const { (void)context; return Maybe<int>(0); }
    int GetStartColumn() const { return 0; }
    int GetEndColumn() const { return 0; }
    MaybeLocal<String> GetSourceLine(Local<Context> context) const { (void)context; return MaybeLocal<String>(); }
    Local<Value> GetScriptResourceName() const { return Local<Value>(); }
    Local<StackTrace> GetStackTrace() const { return Local<StackTrace>(); }
};

class TryCatch {
public:
    TryCatch(Isolate* isolate) { (void)isolate; }
    ~TryCatch() {}
    bool HasCaught() const { return false; }
    Local<Value> Exception() const { return Local<Value>(); }
    Local<v8::Message> Message() const { return Local<v8::Message>(); }
    void Reset() {}
    void SetVerbose(bool value) { (void)value; }
    bool CanContinue() const { return true; }
    bool HasTerminated() const { return false; }
};

class StackTrace {
public:
    static Local<StackTrace> CurrentStackTrace(Isolate* isolate, int frame_limit) {
        (void)isolate; (void)frame_limit;
        return Local<StackTrace>();
    }
    int GetFrameCount() const { return 0; }
};

class StackFrame {
public:
    Local<String> GetScriptName() const { return Local<String>(); }
    Local<String> GetFunctionName() const { return Local<String>(); }
    int GetLineNumber() const { return 0; }
    int GetColumn() const { return 0; }
};

struct HeapStatistics {
    size_t total_heap_size() const { return 0; }
    size_t used_heap_size() const { return 0; }
    size_t heap_size_limit() const { return 0; }
    size_t total_physical_size() const { return 0; }
    size_t external_memory() const { return 0; }
    size_t malloced_memory() const { return 0; }
    size_t peak_malloced_memory() const { return 0; }
    size_t total_available_size() const { return 0; }
};

struct ResourceConstraints {
    void set_max_old_generation_size_in_bytes(size_t v) { (void)v; }
    void set_max_young_generation_size_in_bytes(size_t v) { (void)v; }
};

struct CreateParams {
    ArrayBuffer::Allocator* array_buffer_allocator = nullptr;
    ResourceConstraints constraints;
};

class Isolate {
public:
    using CreateParams = v8::CreateParams;

    class Scope {
    public:
        Scope(Isolate* isolate) { (void)isolate; }
        ~Scope() {}
    };
    static Isolate* New(const CreateParams& params) { (void)params; return new Isolate(); }
    void Dispose() { delete this; }
    void Enter() {}
    void Exit() {}
    Local<Context> GetCurrentContext() { return Local<Context>(); }
    void TerminateExecution() {}
    bool IsExecutionTerminating() { return false; }
    void SetData(uint32_t slot, void* data) { (void)slot; (void)data; }
    void* GetData(uint32_t slot) { (void)slot; return nullptr; }
    void GetHeapStatistics(HeapStatistics* stats) { (void)stats; }
    void LowMemoryNotification() {}
    void RequestGarbageCollectionForTesting(int type) { (void)type; }
    bool InContext() { return false; }
    void SetCaptureStackTraceForUncaughtExceptions(bool capture, int frame_limit = 10) {
        (void)capture; (void)frame_limit;
    }
    void AddMessageListener(void(*callback)(Local<v8::Message>, Local<Value>)) { (void)callback; }
};

class Context {
public:
    class Scope {
    public:
        Scope(Local<Context> context) { (void)context; }
        ~Scope() {}
    };
    static Local<Context> New(Isolate* isolate, nullptr_t ext = nullptr,
                              MaybeLocal<ObjectTemplate> templ = MaybeLocal<ObjectTemplate>()) {
        (void)isolate; (void)ext; (void)templ;
        return Local<Context>();
    }
    Isolate* GetIsolate() { return nullptr; }
    Local<Object> Global() { return Local<Object>(); }
};

class Platform {
public:
    virtual ~Platform() = default;
};

// JSON namespace
namespace JSON {
    inline MaybeLocal<Value> Parse(Local<Context> context, Local<String> json_string) {
        (void)context; (void)json_string;
        return MaybeLocal<Value>();
    }
    inline MaybeLocal<String> Stringify(Local<Context> context, Local<Value> json_object) {
        (void)context; (void)json_object;
        return MaybeLocal<String>();
    }
}

class V8 {
public:
    static void InitializeICU() {}
    static void InitializeICUDefaultLocation(const char* exec_path) { (void)exec_path; }
    static void InitializeExternalStartupData(const char* directory_path) { (void)directory_path; }
    static void InitializeExternalStartupDataFromFile(const char* snapshot_blob) { (void)snapshot_blob; }
    static void InitializeExternalStartupDataFromDirectory(const char* dir) { (void)dir; }
    static void InitializePlatform(Platform* platform) { (void)platform; }
    static void DisposePlatform() {}
    static bool Initialize() { return true; }
    static void ShutdownPlatform() {}
    static bool Dispose() { return true; }
    static void SetFlagsFromString(const char* str) { (void)str; }
    static void SetFlagsFromString(const char* str, size_t length) { (void)str; (void)length; }
    static void SetFlagsFromCommandLine(int* argc, char** argv, bool remove_flags) {
        (void)argc; (void)argv; (void)remove_flags;
    }
    static const char* GetVersion() { return "12.4.0-stub"; }
};

// Undefined/Null helpers
inline Local<Primitive> Undefined(Isolate* isolate) { (void)isolate; return Local<Primitive>(); }
inline Local<Primitive> Null(Isolate* isolate) { (void)isolate; return Local<Primitive>(); }
inline Local<Boolean> True(Isolate* isolate) { (void)isolate; return Local<Boolean>(); }
inline Local<Boolean> False(Isolate* isolate) { (void)isolate; return Local<Boolean>(); }

} // namespace v8
