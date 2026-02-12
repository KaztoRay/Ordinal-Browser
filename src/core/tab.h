#pragma once

/**
 * @file tab.h
 * @brief 브라우저 탭 클래스
 * 
 * 각 탭은 독립된 페이지와 V8 실행 컨텍스트를 가집니다.
 * 탭의 네비게이션 히스토리, 로딩 상태, 보안 정보를 관리합니다.
 */

#include <string>
#include <vector>
#include <memory>
#include <chrono>

namespace ordinal::core {

class Page;
class V8Engine;

/**
 * @brief 탭 로딩 상태
 */
enum class TabLoadingState {
    Idle,           ///< 유휴 상태
    Loading,        ///< 로딩 중
    Loaded,         ///< 로딩 완료
    Error           ///< 로딩 실패
};

/**
 * @brief 보안 수준
 */
enum class SecurityLevel {
    Secure,         ///< HTTPS, 유효한 인증서
    Warning,        ///< 혼합 콘텐츠 등 경고
    Dangerous,      ///< 피싱, 악성코드 의심
    Unknown         ///< 알 수 없음 (file://, about: 등)
};

/**
 * @brief 네비게이션 히스토리 항목
 */
struct HistoryEntry {
    std::string url;                    ///< URL
    std::string title;                  ///< 페이지 제목
    std::chrono::system_clock::time_point visited_at;  ///< 방문 시각
};

/**
 * @brief 브라우저 탭
 * 
 * 각 탭은 독립된 페이지 컨텍스트를 가지며,
 * 네비게이션 히스토리와 보안 상태를 관리합니다.
 */
class Tab {
public:
    /**
     * @brief 탭 생성
     * @param id 탭 고유 ID
     * @param initial_url 초기 URL
     */
    explicit Tab(int id, const std::string& initial_url = "");
    ~Tab();

    // 이동만 허용
    Tab(Tab&&) noexcept;
    Tab& operator=(Tab&&) noexcept;
    Tab(const Tab&) = delete;
    Tab& operator=(const Tab&) = delete;

    // ============================
    // 기본 속성
    // ============================

    [[nodiscard]] int id() const { return id_; }
    [[nodiscard]] const std::string& url() const { return current_url_; }
    [[nodiscard]] const std::string& title() const { return title_; }
    [[nodiscard]] TabLoadingState loadingState() const { return loading_state_; }
    [[nodiscard]] SecurityLevel securityLevel() const { return security_level_; }
    [[nodiscard]] double loadProgress() const { return load_progress_; }
    [[nodiscard]] bool isLoading() const { return loading_state_ == TabLoadingState::Loading; }

    // ============================
    // 네비게이션
    // ============================

    /**
     * @brief URL로 이동
     * @param url 대상 URL
     */
    void navigate(const std::string& url);

    /**
     * @brief 페이지 새로고침
     * @param bypass_cache 캐시 무시 여부
     */
    void reload(bool bypass_cache = false);

    /**
     * @brief 뒤로 가기
     * @return 성공 여부
     */
    bool goBack();

    /**
     * @brief 앞으로 가기
     * @return 성공 여부
     */
    bool goForward();

    /**
     * @brief 로딩 중지
     */
    void stop();

    /**
     * @brief 뒤로 갈 수 있는지 확인
     */
    [[nodiscard]] bool canGoBack() const;

    /**
     * @brief 앞으로 갈 수 있는지 확인
     */
    [[nodiscard]] bool canGoForward() const;

    // ============================
    // 히스토리
    // ============================

    /**
     * @brief 전체 히스토리 조회
     */
    [[nodiscard]] const std::vector<HistoryEntry>& history() const { return history_; }

    /**
     * @brief 히스토리 초기화
     */
    void clearHistory();

    // ============================
    // 페이지 접근
    // ============================

    /**
     * @brief 현재 페이지 조회
     */
    [[nodiscard]] Page* currentPage() const { return current_page_.get(); }

    // ============================
    // JavaScript 실행
    // ============================

    /**
     * @brief 현재 페이지에서 JavaScript 실행
     * @param script JavaScript 코드
     * @return 실행 결과 문자열
     */
    std::string executeJavaScript(const std::string& script);

private:
    int id_;
    std::string current_url_;
    std::string title_;
    TabLoadingState loading_state_{TabLoadingState::Idle};
    SecurityLevel security_level_{SecurityLevel::Unknown};
    double load_progress_{0.0};

    /// 네비게이션 히스토리
    std::vector<HistoryEntry> history_;
    int history_index_{-1};

    /// 현재 페이지
    std::unique_ptr<Page> current_page_;

    /// 탭 전용 V8 엔진 (독립 컨텍스트)
    std::unique_ptr<V8Engine> v8_engine_;

    /**
     * @brief 히스토리에 항목 추가
     */
    void pushHistory(const std::string& url, const std::string& title);

    /**
     * @brief URL 스킴에 따른 보안 수준 판별
     */
    SecurityLevel evaluateSecurityLevel(const std::string& url) const;
};

} // namespace ordinal::core
