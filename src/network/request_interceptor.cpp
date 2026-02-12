/**
 * @file request_interceptor.cpp
 * @brief 네트워크 요청 인터셉터 구현
 */

#include "request_interceptor.h"

#include <iostream>
#include <algorithm>
#include <chrono>

namespace ordinal::network {

RequestInterceptor::RequestInterceptor() {
    std::cout << "[RequestInterceptor] 요청 인터셉터 초기화" << std::endl;
}

RequestInterceptor::~RequestInterceptor() = default;

void RequestInterceptor::addMiddleware(std::shared_ptr<InterceptMiddleware> middleware) {
    if (!middleware) return;

    std::cout << "[RequestInterceptor] 미들웨어 추가: " << middleware->name() 
              << " (우선순위: " << middleware->priority() << ")" << std::endl;

    middlewares_.push_back(std::move(middleware));
    sortMiddlewares();
}

void RequestInterceptor::removeMiddleware(const std::string& name) {
    auto it = std::remove_if(middlewares_.begin(), middlewares_.end(),
        [&name](const auto& m) { return m->name() == name; });
    
    if (it != middlewares_.end()) {
        middlewares_.erase(it, middlewares_.end());
        std::cout << "[RequestInterceptor] 미들웨어 제거: " << name << std::endl;
    }
}

InterceptResult RequestInterceptor::interceptRequest(const HttpRequest& request) {
    total_inspected_.fetch_add(1, std::memory_order_relaxed);

    auto start = std::chrono::high_resolution_clock::now();
    InterceptResult final_result;
    final_result.action = InterceptAction::Allow;

    // 모든 미들웨어를 우선순위 순으로 실행
    for (const auto& middleware : middlewares_) {
        if (!middleware->isEnabled()) continue;

        try {
            auto result = middleware->onRequest(request);

            // 차단이면 즉시 반환
            if (result.action == InterceptAction::Block) {
                total_blocked_.fetch_add(1, std::memory_order_relaxed);

                auto end = std::chrono::high_resolution_clock::now();
                result.analysis_time_ms = std::chrono::duration<double, std::milli>(
                    end - start
                ).count();

                std::cout << "[RequestInterceptor] 요청 차단: " << request.url
                          << " (사유: " << result.reason_detail 
                          << ", 미들웨어: " << middleware->name() << ")" << std::endl;

                return result;
            }

            // 수정이면 수정된 요청 기록
            if (result.action == InterceptAction::Modify) {
                final_result = result;
            }

            // 리다이렉트이면 기록
            if (result.action == InterceptAction::Redirect) {
                final_result = result;
            }

        } catch (const std::exception& e) {
            std::cerr << "[RequestInterceptor] 미들웨어 오류 (" 
                      << middleware->name() << "): " << e.what() << std::endl;
            // 미들웨어 오류는 무시하고 계속 진행
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    final_result.analysis_time_ms = std::chrono::duration<double, std::milli>(
        end - start
    ).count();

    return final_result;
}

InterceptResult RequestInterceptor::interceptResponse(
    const HttpRequest& request,
    const HttpResponse& response
) {
    auto start = std::chrono::high_resolution_clock::now();
    InterceptResult final_result;
    final_result.action = InterceptAction::Allow;

    for (const auto& middleware : middlewares_) {
        if (!middleware->isEnabled()) continue;

        try {
            auto result = middleware->onResponse(request, response);

            if (result.action == InterceptAction::Block) {
                total_blocked_.fetch_add(1, std::memory_order_relaxed);

                auto end = std::chrono::high_resolution_clock::now();
                result.analysis_time_ms = std::chrono::duration<double, std::milli>(
                    end - start
                ).count();

                std::cout << "[RequestInterceptor] 응답 차단: " << request.url
                          << " (사유: " << result.reason_detail << ")" << std::endl;

                return result;
            }

        } catch (const std::exception& e) {
            std::cerr << "[RequestInterceptor] 응답 검사 오류 (" 
                      << middleware->name() << "): " << e.what() << std::endl;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    final_result.analysis_time_ms = std::chrono::duration<double, std::milli>(
        end - start
    ).count();

    return final_result;
}

void RequestInterceptor::resetStats() {
    total_inspected_.store(0, std::memory_order_relaxed);
    total_blocked_.store(0, std::memory_order_relaxed);
}

void RequestInterceptor::sortMiddlewares() {
    std::sort(middlewares_.begin(), middlewares_.end(),
        [](const auto& a, const auto& b) {
            return a->priority() < b->priority();
        });
}

} // namespace ordinal::network
