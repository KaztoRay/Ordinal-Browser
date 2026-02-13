// V8 libplatform 스텁 헤더
#pragma once
#include <memory>

namespace v8 {
class Platform;
namespace platform {

inline std::unique_ptr<v8::Platform> NewDefaultPlatform(
    int thread_pool_size = 0,
    bool idle_task_support = false) {
    (void)thread_pool_size;
    (void)idle_task_support;
    return nullptr;
}

} // namespace platform
} // namespace v8
