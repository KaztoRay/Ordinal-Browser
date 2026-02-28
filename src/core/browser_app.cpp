/**
 * @file browser_app.cpp
 * @brief 브라우저 애플리케이션 메인 클래스 구현
 */

#include "browser_app.h"
#include "v8_engine.h"
#include "tab.h"

#include <iostream>
#include <algorithm>
#include <filesystem>

namespace ordinal::core {

// 싱글톤 인스턴스
BrowserApp& BrowserApp::instance() {
    static BrowserApp app;
    return app;
}

BrowserApp::~BrowserApp() {
    if (state_ == BrowserState::Running) {
        shutdown();
    }
}

bool BrowserApp::initialize(const BrowserConfig& config) {
    if (state_ != BrowserState::Uninitialized) {
        std::cerr << "[BrowserApp] 이미 초기화되었거나 실행 중입니다." << std::endl;
        return false;
    }

    state_ = BrowserState::Initializing;
    config_ = config;

    std::cout << "[BrowserApp] OrdinalV8 초기화 시작..." << std::endl;

    // 1. 사용자 데이터 디렉토리 생성
    if (!config_.user_data_dir.empty()) {
        std::filesystem::create_directories(config_.user_data_dir);
        std::cout << "[BrowserApp] 사용자 데이터 디렉토리: " << config_.user_data_dir << std::endl;
    }

    // 2. 캐시 디렉토리 생성
    if (!config_.cache_dir.empty()) {
        std::filesystem::create_directories(config_.cache_dir);
    }

    // 3. V8 엔진 초기화
    V8EngineConfig v8_config;
    v8_config.max_heap_size_mb = 512;
    v8_config.enable_harmony = true;
    v8_config.enable_wasm = false;  // 보안상 기본 비활성화

    if (!V8Engine::initialize(v8_config)) {
        std::cerr << "[BrowserApp] V8 엔진 초기화 실패!" << std::endl;
        state_ = BrowserState::Uninitialized;
        return false;
    }

    v8_engine_ = std::make_unique<V8Engine>(v8_config);
    std::cout << "[BrowserApp] V8 엔진 초기화 완료" << std::endl;

    // 4. 보안 서브시스템 초기화
    if (config_.enable_security_agent) {
        std::cout << "[BrowserApp] 보안 에이전트 활성화됨 (gRPC 포트: " 
                  << config_.grpc_port << ")" << std::endl;
        // SecurityAgent 초기화는 security 모듈에서 처리
    }

    // 5. 프라이버시 보호 초기화
    if (config_.enable_privacy_protection) {
        std::cout << "[BrowserApp] 프라이버시 보호 활성화됨" << std::endl;
    }

    state_ = BrowserState::Running;
    std::cout << "[BrowserApp] OrdinalV8 초기화 완료!" << std::endl;

    // 초기 탭 생성
    createTab(config_.homepage);

    return true;
}

void BrowserApp::shutdown() {
    if (state_ != BrowserState::Running) return;

    state_ = BrowserState::ShuttingDown;
    std::cout << "[BrowserApp] 종료 절차 시작..." << std::endl;

    // 모든 탭 닫기
    tabs_.clear();
    active_tab_id_ = -1;

    // V8 엔진 종료
    v8_engine_.reset();
    V8Engine::shutdown();

    state_ = BrowserState::Terminated;
    std::cout << "[BrowserApp] OrdinalV8 종료 완료" << std::endl;
}

// ============================================================
// 탭 관리
// ============================================================

int BrowserApp::createTab(const std::string& url) {
    if (state_ != BrowserState::Running) {
        std::cerr << "[BrowserApp] 브라우저가 실행 중이 아닙니다." << std::endl;
        return -1;
    }

    if (tabCount() >= config_.max_tabs) {
        std::cerr << "[BrowserApp] 최대 탭 수 초과 (" << config_.max_tabs << ")" << std::endl;
        return -1;
    }

    int tab_id = next_tab_id_++;
    auto tab = std::make_unique<Tab>(tab_id, url);
    tabs_.push_back(std::move(tab));

    // 첫 번째 탭이면 자동 활성화
    if (active_tab_id_ < 0) {
        active_tab_id_ = tab_id;
    }

    dispatchEvent(BrowserEvent::TabCreated, std::to_string(tab_id));
    std::cout << "[BrowserApp] 새 탭 생성: #" << tab_id 
              << (url.empty() ? "" : " → " + url) << std::endl;

    return tab_id;
}

void BrowserApp::closeTab(int tab_id) {
    auto it = std::find_if(tabs_.begin(), tabs_.end(),
        [tab_id](const auto& tab) { return tab->id() == tab_id; });

    if (it == tabs_.end()) {
        std::cerr << "[BrowserApp] 탭을 찾을 수 없습니다: #" << tab_id << std::endl;
        return;
    }

    // 활성 탭이면 다른 탭으로 전환
    if (active_tab_id_ == tab_id) {
        if (tabs_.size() > 1) {
            // 이전 탭 또는 다음 탭으로 전환
            auto idx = std::distance(tabs_.begin(), it);
            if (idx > 0) {
                active_tab_id_ = tabs_[idx - 1]->id();
            } else {
                active_tab_id_ = tabs_[idx + 1]->id();
            }
        } else {
            active_tab_id_ = -1;
        }
    }

    tabs_.erase(it);
    dispatchEvent(BrowserEvent::TabClosed, std::to_string(tab_id));
    std::cout << "[BrowserApp] 탭 닫힘: #" << tab_id << std::endl;
}

void BrowserApp::activateTab(int tab_id) {
    auto* tab = getTab(tab_id);
    if (!tab) {
        std::cerr << "[BrowserApp] 탭을 찾을 수 없습니다: #" << tab_id << std::endl;
        return;
    }

    active_tab_id_ = tab_id;
    dispatchEvent(BrowserEvent::TabActivated, std::to_string(tab_id));
}

Tab* BrowserApp::activeTab() const {
    if (active_tab_id_ < 0) return nullptr;
    return getTab(active_tab_id_);
}

Tab* BrowserApp::getTab(int tab_id) const {
    auto it = std::find_if(tabs_.begin(), tabs_.end(),
        [tab_id](const auto& tab) { return tab->id() == tab_id; });
    return (it != tabs_.end()) ? it->get() : nullptr;
}

// ============================================================
// 이벤트 시스템
// ============================================================

void BrowserApp::addEventListener(BrowserEvent event, EventCallback callback) {
    event_listeners_[event].push_back(std::move(callback));
}

void BrowserApp::dispatchEvent(BrowserEvent event, const std::string& data) {
    auto it = event_listeners_.find(event);
    if (it != event_listeners_.end()) {
        for (const auto& callback : it->second) {
            callback(event, data);
        }
    }
}

} // namespace ordinal::core
