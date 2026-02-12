/**
 * @file tab.cpp
 * @brief 브라우저 탭 구현
 */

#include "tab.h"
#include "page.h"
#include "v8_engine.h"

#include <iostream>
#include <algorithm>

namespace ordinal::core {

Tab::Tab(int id, const std::string& initial_url)
    : id_(id)
    , current_url_(initial_url)
    , title_("새 탭")
    , current_page_(std::make_unique<Page>())
    , v8_engine_(std::make_unique<V8Engine>()) {
    
    // 보안 관련 네이티브 함수 등록
    v8_engine_->registerNativeFunction("__ordinal_check_url", 
        [](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "false";
            // 보안 에이전트에 URL 검사 요청 (추후 연동)
            return "true";
        });

    // 초기 URL이 있으면 네비게이션
    if (!initial_url.empty() && initial_url != "about:blank") {
        navigate(initial_url);
    }

    std::cout << "[Tab] 탭 #" << id_ << " 생성됨" << std::endl;
}

Tab::~Tab() {
    std::cout << "[Tab] 탭 #" << id_ << " 소멸됨" << std::endl;
}

Tab::Tab(Tab&&) noexcept = default;
Tab& Tab::operator=(Tab&&) noexcept = default;

// ============================================================
// 네비게이션
// ============================================================

void Tab::navigate(const std::string& url) {
    if (url.empty()) return;

    std::cout << "[Tab] #" << id_ << " 네비게이션: " << url << std::endl;

    // 로딩 상태 변경
    loading_state_ = TabLoadingState::Loading;
    load_progress_ = 0.0;

    // URL 보안 수준 평가
    security_level_ = evaluateSecurityLevel(url);

    // 히스토리에 추가 (현재 위치 이후의 항목 제거)
    if (history_index_ >= 0 && 
        history_index_ < static_cast<int>(history_.size()) - 1) {
        history_.erase(
            history_.begin() + history_index_ + 1,
            history_.end()
        );
    }

    current_url_ = url;
    pushHistory(url, title_);

    // V8 컨텍스트 초기화 (새 페이지)
    v8_engine_->resetContext();

    // 페이지 로딩 시뮬레이션 (실제로는 네트워크 레이어 사용)
    // TODO: HttpClient를 통한 실제 네트워크 요청
    current_page_ = std::make_unique<Page>();
    current_page_->setUrl(url);

    // 로딩 완료
    loading_state_ = TabLoadingState::Loaded;
    load_progress_ = 100.0;

    std::cout << "[Tab] #" << id_ << " 로딩 완료: " << url << std::endl;
}

void Tab::reload(bool bypass_cache) {
    if (current_url_.empty()) return;

    std::cout << "[Tab] #" << id_ << " 새로고침" 
              << (bypass_cache ? " (캐시 무시)" : "") << std::endl;

    // 현재 URL로 재네비게이션
    // bypass_cache가 true면 캐시 헤더에 no-cache 추가
    navigate(current_url_);
}

bool Tab::goBack() {
    if (!canGoBack()) return false;

    history_index_--;
    const auto& entry = history_[history_index_];
    
    // 히스토리 추가 없이 직접 URL 변경
    current_url_ = entry.url;
    title_ = entry.title;
    security_level_ = evaluateSecurityLevel(current_url_);

    std::cout << "[Tab] #" << id_ << " 뒤로: " << current_url_ << std::endl;
    return true;
}

bool Tab::goForward() {
    if (!canGoForward()) return false;

    history_index_++;
    const auto& entry = history_[history_index_];
    
    current_url_ = entry.url;
    title_ = entry.title;
    security_level_ = evaluateSecurityLevel(current_url_);

    std::cout << "[Tab] #" << id_ << " 앞으로: " << current_url_ << std::endl;
    return true;
}

void Tab::stop() {
    if (loading_state_ == TabLoadingState::Loading) {
        loading_state_ = TabLoadingState::Idle;
        std::cout << "[Tab] #" << id_ << " 로딩 중지" << std::endl;
    }
}

bool Tab::canGoBack() const {
    return history_index_ > 0;
}

bool Tab::canGoForward() const {
    return history_index_ < static_cast<int>(history_.size()) - 1;
}

// ============================================================
// 히스토리
// ============================================================

void Tab::pushHistory(const std::string& url, const std::string& title) {
    HistoryEntry entry;
    entry.url = url;
    entry.title = title;
    entry.visited_at = std::chrono::system_clock::now();

    history_.push_back(std::move(entry));
    history_index_ = static_cast<int>(history_.size()) - 1;
}

void Tab::clearHistory() {
    history_.clear();
    history_index_ = -1;
    std::cout << "[Tab] #" << id_ << " 히스토리 초기화" << std::endl;
}

// ============================================================
// JavaScript 실행
// ============================================================

std::string Tab::executeJavaScript(const std::string& script) {
    if (!v8_engine_) {
        return "[오류] V8 엔진이 초기화되지 않았습니다.";
    }

    auto result = v8_engine_->executeScript(script, current_url_);
    if (result.success) {
        return result.value;
    } else {
        std::cerr << "[Tab] #" << id_ << " JS 오류: " 
                  << result.error_message << " (라인 " 
                  << result.line_number << ")" << std::endl;
        return "[오류] " + result.error_message;
    }
}

// ============================================================
// 보안 평가
// ============================================================

SecurityLevel Tab::evaluateSecurityLevel(const std::string& url) const {
    // URL 스킴 기반 기본 보안 수준 판별
    if (url.starts_with("https://")) {
        return SecurityLevel::Secure;
    } else if (url.starts_with("http://")) {
        return SecurityLevel::Warning;  // 암호화되지 않은 연결
    } else if (url.starts_with("file://") || 
               url.starts_with("about:") || 
               url.starts_with("data:")) {
        return SecurityLevel::Unknown;
    } else {
        return SecurityLevel::Warning;
    }
}

} // namespace ordinal::core
