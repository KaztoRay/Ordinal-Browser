#pragma once

/**
 * @file browser_app.h
 * @brief 브라우저 애플리케이션 메인 클래스
 * 
 * 전체 브라우저 수명 주기를 관리하는 최상위 클래스입니다.
 * V8 엔진, 탭 관리, 보안 시스템 등 모든 서브시스템을 초기화하고 조율합니다.
 */

#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace ordinal::core {

class Tab;
class V8Engine;

/**
 * @brief 브라우저 설정 구조체
 */
struct BrowserConfig {
    std::string user_data_dir;              ///< 사용자 데이터 디렉토리
    std::string cache_dir;                  ///< 캐시 디렉토리
    bool enable_security_agent{true};       ///< 보안 에이전트 활성화
    bool enable_privacy_protection{true};   ///< 프라이버시 보호 활성화
    bool enable_dev_tools{true};            ///< 개발자 도구 활성화
    uint16_t grpc_port{50051};              ///< gRPC 서버 포트 (Python Agent 통신)
    int max_tabs{50};                       ///< 최대 탭 수
    std::string homepage{"about:blank"};    ///< 홈페이지 URL
    std::string search_engine{"https://duckduckgo.com/?q="}; ///< 기본 검색 엔진
};

/**
 * @brief 브라우저 상태 열거형
 */
enum class BrowserState {
    Uninitialized,      ///< 초기화 전
    Initializing,       ///< 초기화 중
    Running,            ///< 실행 중
    ShuttingDown,       ///< 종료 중
    Terminated          ///< 종료됨
};

/**
 * @brief 브라우저 이벤트 타입
 */
enum class BrowserEvent {
    TabCreated,
    TabClosed,
    TabActivated,
    NavigationStarted,
    NavigationCompleted,
    SecurityAlert,
    DownloadStarted,
    DownloadCompleted,
};

/**
 * @brief 이벤트 콜백 타입
 */
using EventCallback = std::function<void(BrowserEvent, const std::string&)>;

/**
 * @brief 메인 브라우저 애플리케이션 클래스
 * 
 * 싱글톤 패턴으로 구현됩니다.
 * Qt 애플리케이션 이벤트 루프와 통합되어 동작합니다.
 */
class BrowserApp {
public:
    /**
     * @brief 싱글톤 인스턴스 획득
     */
    static BrowserApp& instance();

    // 복사/이동 금지
    BrowserApp(const BrowserApp&) = delete;
    BrowserApp& operator=(const BrowserApp&) = delete;

    /**
     * @brief 브라우저 초기화
     * @param config 브라우저 설정
     * @return 초기화 성공 여부
     */
    bool initialize(const BrowserConfig& config = {});

    /**
     * @brief 브라우저 종료
     */
    void shutdown();

    /**
     * @brief 현재 상태 조회
     */
    [[nodiscard]] BrowserState state() const { return state_; }

    // ============================
    // 탭 관리
    // ============================

    /**
     * @brief 새 탭 생성
     * @param url 초기 URL (비어있으면 빈 탭)
     * @return 생성된 탭의 ID
     */
    int createTab(const std::string& url = "");

    /**
     * @brief 탭 닫기
     * @param tab_id 탭 ID
     */
    void closeTab(int tab_id);

    /**
     * @brief 활성 탭 전환
     * @param tab_id 활성화할 탭 ID
     */
    void activateTab(int tab_id);

    /**
     * @brief 현재 활성 탭 조회
     * @return 활성 탭 포인터 (없으면 nullptr)
     */
    [[nodiscard]] Tab* activeTab() const;

    /**
     * @brief 탭 ID로 탭 조회
     * @return 탭 포인터 (없으면 nullptr)
     */
    [[nodiscard]] Tab* getTab(int tab_id) const;

    /**
     * @brief 전체 탭 목록 조회
     */
    [[nodiscard]] const std::vector<std::unique_ptr<Tab>>& tabs() const { return tabs_; }

    /**
     * @brief 열린 탭 수
     */
    [[nodiscard]] int tabCount() const { return static_cast<int>(tabs_.size()); }

    // ============================
    // 이벤트
    // ============================

    /**
     * @brief 이벤트 리스너 등록
     * @param event 이벤트 타입
     * @param callback 콜백 함수
     */
    void addEventListener(BrowserEvent event, EventCallback callback);

    // ============================
    // 설정
    // ============================

    /**
     * @brief 현재 설정 조회
     */
    [[nodiscard]] const BrowserConfig& config() const { return config_; }

    /**
     * @brief V8 엔진 인스턴스 조회
     */
    [[nodiscard]] V8Engine* v8Engine() const { return v8_engine_.get(); }

private:
    BrowserApp() = default;
    ~BrowserApp();

    /**
     * @brief 이벤트 발송
     */
    void dispatchEvent(BrowserEvent event, const std::string& data = "");

    BrowserConfig config_;
    BrowserState state_{BrowserState::Uninitialized};
    std::vector<std::unique_ptr<Tab>> tabs_;
    int active_tab_id_{-1};
    int next_tab_id_{1};
    std::unique_ptr<V8Engine> v8_engine_;

    /// 이벤트 리스너 맵
    std::unordered_map<BrowserEvent, std::vector<EventCallback>> event_listeners_;
};

} // namespace ordinal::core
