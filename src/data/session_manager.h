#pragma once

/**
 * @file session_manager.h
 * @brief 세션 관리자
 *
 * 탭 상태를 JSON으로 저장/복원, 자동 저장 타이머, 크래시 복구,
 * 명명된 세션, 시작 시 복원 설정을 관리합니다.
 */

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <optional>
#include <cstdint>

namespace ordinal::data {

class DataStore;

// ============================================================
// 탭 상태 정보
// ============================================================

/**
 * @brief 탭 네비게이션 히스토리 항목
 */
struct NavHistoryEntry {
    std::string url;        ///< 페이지 URL
    std::string title;      ///< 페이지 제목
    int64_t timestamp{0};   ///< 방문 시간 (Unix epoch ms)
};

/**
 * @brief 저장된 탭 상태
 */
struct TabState {
    int tab_index{0};               ///< 탭 인덱스 (순서)
    std::string current_url;        ///< 현재 URL
    std::string title;              ///< 현재 제목
    std::string favicon_url;        ///< 파비콘 URL
    bool pinned{false};             ///< 고정 탭 여부
    bool muted{false};              ///< 음소거 여부
    int scroll_x{0};                ///< 가로 스크롤 위치
    int scroll_y{0};                ///< 세로 스크롤 위치
    double zoom_factor{1.0};        ///< 줌 배율

    /// 뒤로/앞으로 네비게이션 히스토리
    std::vector<NavHistoryEntry> nav_history;
    int nav_index{-1};              ///< 현재 히스토리 인덱스 (-1이면 마지막)

    /// 폼 데이터 (페이지 내 입력 필드 상태)
    std::unordered_map<std::string, std::string> form_data;
};

/**
 * @brief 창 상태
 */
struct WindowState {
    int window_id{0};           ///< 창 ID
    int x{100};                 ///< 창 X 좌표
    int y{100};                 ///< 창 Y 좌표
    int width{1280};            ///< 창 너비
    int height{800};            ///< 창 높이
    bool maximized{false};      ///< 최대화 여부
    bool fullscreen{false};     ///< 전체 화면 여부
    int active_tab_index{0};    ///< 활성 탭 인덱스
    std::vector<TabState> tabs; ///< 탭 목록
};

// ============================================================
// 세션 데이터
// ============================================================

/**
 * @brief 세션 전체 데이터
 */
struct SessionData {
    std::string name;                       ///< 세션 이름 (빈 문자열이면 기본)
    std::string created_at;                 ///< 생성 시간 (ISO8601)
    std::string modified_at;                ///< 마지막 수정 시간
    std::vector<WindowState> windows;       ///< 창 목록

    /// 세션 메타데이터 (확장 가능)
    std::unordered_map<std::string, std::string> metadata;
};

/**
 * @brief 세션 요약 (목록 표시용)
 */
struct SessionSummary {
    int64_t id{0};              ///< DB ID
    std::string name;           ///< 세션 이름
    std::string created_at;     ///< 생성 시간
    std::string modified_at;    ///< 수정 시간
    int window_count{0};        ///< 창 수
    int tab_count{0};           ///< 총 탭 수
    bool is_auto_save{false};   ///< 자동 저장 세션 여부
    bool is_crash_recovery{false}; ///< 크래시 복구 세션 여부
};

// ============================================================
// 시작 설정
// ============================================================

/**
 * @brief 브라우저 시작 시 동작
 */
enum class StartupAction {
    NewTab,             ///< 새 탭으로 시작
    RestoreLastSession, ///< 마지막 세션 복원
    OpenHomePage,       ///< 홈 페이지 열기
    OpenSpecificPages,  ///< 지정된 페이지 열기
    RestoreNamedSession ///< 명명된 세션 복원
};

/**
 * @brief 세션 관리자 설정
 */
struct SessionConfig {
    StartupAction startup_action{StartupAction::RestoreLastSession};
    std::string home_page_url{"about:blank"};   ///< 홈 페이지 URL
    std::vector<std::string> startup_urls;       ///< 시작 시 열 URL 목록
    std::string named_session;                   ///< 시작 시 복원할 세션 이름

    int auto_save_interval_sec{30};              ///< 자동 저장 간격 (초)
    bool auto_save_enabled{true};                ///< 자동 저장 활성화
    bool crash_recovery_enabled{true};           ///< 크래시 복구 활성화
    int max_saved_sessions{50};                  ///< 최대 저장 세션 수
    bool save_form_data{true};                   ///< 폼 데이터 저장 여부
    bool save_scroll_position{true};             ///< 스크롤 위치 저장 여부
};

// ============================================================
// 콜백 타입
// ============================================================

/// 세션 복원 콜백: (WindowState) — 창과 탭을 실제로 생성
using SessionRestoreCallback = std::function<void(const WindowState&)>;

/// 현재 탭 상태 수집 콜백: () → vector<WindowState>
using SessionCollectCallback = std::function<std::vector<WindowState>()>;

// ============================================================
// SessionManager
// ============================================================

/**
 * @brief 세션 관리자
 *
 * 탭/창 상태를 JSON으로 직렬화하여 SQLite에 저장하고,
 * 브라우저 시작 시 복원하거나 크래시 복구에 사용합니다.
 */
class SessionManager {
public:
    explicit SessionManager(std::shared_ptr<DataStore> store);
    ~SessionManager();

    // 복사 금지
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    /**
     * @brief 초기화 (테이블 생성, 자동 저장 시작)
     * @param config 세션 설정
     * @return 성공 여부
     */
    bool initialize(const SessionConfig& config = {});

    /**
     * @brief 종료 (자동 저장 중지, 마지막 세션 저장)
     */
    void shutdown();

    // ============================
    // 세션 저장
    // ============================

    /**
     * @brief 현재 세션 저장
     * @param name 세션 이름 (빈 문자열이면 "__autosave__")
     * @return 세션 DB ID
     */
    int64_t saveSession(const std::string& name = "");

    /**
     * @brief 세션 데이터를 직접 저장
     * @param session 세션 데이터
     * @return 세션 DB ID
     */
    int64_t saveSession(const SessionData& session);

    /**
     * @brief 크래시 복구용 세션 저장
     *
     * 별도 파일에 즉시 플러시 (자동 저장보다 안정적)
     */
    bool saveCrashRecoverySession();

    // ============================
    // 세션 복원
    // ============================

    /**
     * @brief 마지막 자동 저장 세션 복원
     * @return 성공 여부
     */
    bool restoreLastSession();

    /**
     * @brief 명명된 세션 복원
     * @param name 세션 이름
     * @return 성공 여부
     */
    bool restoreSession(const std::string& name);

    /**
     * @brief ID로 세션 복원
     * @param session_id 세션 DB ID
     * @return 성공 여부
     */
    bool restoreSession(int64_t session_id);

    /**
     * @brief 크래시 복구 세션이 존재하는지 확인
     * @return 존재 여부
     */
    [[nodiscard]] bool hasCrashRecoverySession() const;

    /**
     * @brief 크래시 복구 세션 복원
     * @return 성공 여부
     */
    bool restoreCrashRecoverySession();

    /**
     * @brief 시작 설정에 따라 자동 복원
     * @return 복원된 탭 수
     */
    int performStartupRestore();

    // ============================
    // 세션 조회
    // ============================

    /**
     * @brief 저장된 세션 목록 조회
     * @param include_auto 자동 저장 세션 포함 여부
     * @return 세션 요약 목록
     */
    [[nodiscard]] std::vector<SessionSummary> listSessions(
        bool include_auto = false) const;

    /**
     * @brief 세션 데이터 조회
     * @param session_id 세션 DB ID
     * @return 세션 데이터
     */
    [[nodiscard]] std::optional<SessionData> getSession(int64_t session_id) const;

    /**
     * @brief 이름으로 세션 데이터 조회
     */
    [[nodiscard]] std::optional<SessionData> getSession(
        const std::string& name) const;

    // ============================
    // 세션 관리
    // ============================

    /**
     * @brief 세션 이름 변경
     */
    bool renameSession(int64_t session_id, const std::string& new_name);

    /**
     * @brief 세션 삭제
     */
    bool deleteSession(int64_t session_id);

    /**
     * @brief 이름으로 세션 삭제
     */
    bool deleteSession(const std::string& name);

    /**
     * @brief 자동 저장 세션 정리 (오래된 것부터)
     * @return 삭제된 수
     */
    int cleanupAutoSaves();

    /**
     * @brief 크래시 복구 세션 삭제 (정상 종료 시)
     */
    void clearCrashRecoverySession();

    // ============================
    // 콜백 등록
    // ============================

    /**
     * @brief 복원 콜백 설정 (세션 복원 시 창/탭 생성 담당)
     */
    void setRestoreCallback(SessionRestoreCallback callback);

    /**
     * @brief 수집 콜백 설정 (현재 탭 상태 수집 담당)
     */
    void setCollectCallback(SessionCollectCallback callback);

    // ============================
    // 설정
    // ============================

    /**
     * @brief 현재 설정 조회
     */
    [[nodiscard]] const SessionConfig& config() const { return config_; }

    /**
     * @brief 설정 업데이트 (자동 저장 재시작)
     */
    void updateConfig(const SessionConfig& config);

    // ============================
    // 직렬화 유틸리티
    // ============================

    /**
     * @brief SessionData → JSON 문자열
     */
    static std::string serializeToJson(const SessionData& session);

    /**
     * @brief JSON 문자열 → SessionData
     */
    static std::optional<SessionData> deserializeFromJson(const std::string& json);

    /**
     * @brief 세션을 파일로 내보내기
     */
    bool exportSession(int64_t session_id, const std::string& file_path) const;

    /**
     * @brief 파일에서 세션 가져오기
     * @return 가져온 세션 DB ID
     */
    int64_t importSession(const std::string& file_path);

private:
    std::shared_ptr<DataStore> store_;
    SessionConfig config_;
    mutable std::mutex mutex_;

    // 자동 저장 타이머
    std::thread auto_save_thread_;
    std::atomic<bool> running_{false};

    // 콜백
    SessionRestoreCallback restore_callback_;
    SessionCollectCallback collect_callback_;

    /**
     * @brief 자동 저장 루프
     */
    void autoSaveLoop();

    /**
     * @brief 세션을 DB에 저장 (내부)
     * @param name 세션 이름
     * @param json JSON 데이터
     * @param is_auto 자동 저장 여부
     * @param is_crash 크래시 복구 여부
     * @return DB ID
     */
    int64_t persistSession(const std::string& name,
                            const std::string& json,
                            bool is_auto, bool is_crash);

    /**
     * @brief 현재 시간 ISO8601 문자열
     */
    static std::string nowIso8601();

    /**
     * @brief JSON 문자열 이스케이프
     */
    static std::string jsonEscape(const std::string& s);

    /**
     * @brief JSON 문자열 언이스케이프
     */
    static std::string jsonUnescape(const std::string& s);
};

} // namespace ordinal::data
