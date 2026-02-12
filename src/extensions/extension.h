#pragma once

/**
 * @file extension.h
 * @brief 확장 프로그램 클래스
 * 
 * 개별 확장 프로그램의 매니페스트 데이터, 콘텐츠 스크립트, 백그라운드 스크립트,
 * 팝업 UI, 권한 요청, 메시지 패싱(sendMessage/onMessage), 스토리지 API를 관리합니다.
 */

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <optional>
#include <variant>
#include <chrono>
#include <mutex>
#include <atomic>

namespace ordinal::extensions {

// 전방 선언
class ContentScript;
class ExtensionAPI;
class ExtensionManager;

/**
 * @brief 확장 프로그램 권한 타입
 */
enum class Permission {
    Tabs,               ///< 탭 접근 (URL, 제목 등)
    ActiveTab,          ///< 현재 활성 탭에 대한 임시 접근
    Storage,            ///< 확장 스토리지 (sync/local)
    WebRequest,         ///< 네트워크 요청 가로채기
    WebRequestBlocking, ///< 네트워크 요청 차단 (동기)
    Cookies,            ///< 쿠키 접근
    Downloads,          ///< 다운로드 관리
    History,            ///< 방문 기록 접근
    Bookmarks,          ///< 북마크 접근
    Notifications,      ///< 알림 표시
    ContextMenus,       ///< 컨텍스트 메뉴 추가
    ClipboardRead,      ///< 클립보드 읽기
    ClipboardWrite,     ///< 클립보드 쓰기
    Identity,           ///< 사용자 인증 정보
    Management,         ///< 다른 확장 관리
    WebNavigation,      ///< 웹 내비게이션 이벤트
    Alarms,             ///< 알람/타이머
    UnlimitedStorage    ///< 무제한 스토리지
};

/**
 * @brief 권한 이름 ↔ 열거형 변환 유틸리티
 */
class PermissionUtils {
public:
    /// 문자열 → Permission 변환
    static std::optional<Permission> fromString(const std::string& name);
    /// Permission → 문자열 변환
    static std::string toString(Permission perm);
    /// 위험 권한 여부 판단
    static bool isDangerous(Permission perm);
};

/**
 * @brief 확장 프로그램 상태
 */
enum class ExtensionState {
    Unloaded,       ///< 로드되지 않음
    Loading,        ///< 로딩 중
    Active,         ///< 활성
    Disabled,       ///< 비활성 (사용자에 의해)
    Error,          ///< 오류 발생
    Suspended       ///< 일시 중단 (메모리 절약)
};

/**
 * @brief 확장 프로그램 매니페스트 (manifest.json 파싱 결과)
 */
struct ExtensionManifest {
    int manifest_version{3};                        ///< 매니페스트 버전 (2 또는 3)
    std::string name;                               ///< 확장 이름
    std::string version;                            ///< 확장 버전
    std::string description;                        ///< 확장 설명
    std::string author;                             ///< 개발자
    std::string homepage_url;                       ///< 홈페이지 URL

    /// 아이콘 경로 (크기 → 파일 경로)
    std::unordered_map<int, std::string> icons;

    /// 요청 권한 목록
    std::unordered_set<Permission> permissions;

    /// 선택적 권한 (사용자 동의 필요)
    std::unordered_set<Permission> optional_permissions;

    /// 호스트 권한 패턴 (예: "*://*.google.com/*")
    std::vector<std::string> host_permissions;

    /// 백그라운드 서비스 워커 경로
    std::string background_service_worker;

    /// 백그라운드 스크립트 목록 (MV2 호환)
    std::vector<std::string> background_scripts;

    /// 콘텐츠 스크립트 정의
    struct ContentScriptDef {
        std::vector<std::string> matches;           ///< URL 매치 패턴
        std::vector<std::string> exclude_matches;   ///< 제외 패턴
        std::vector<std::string> js;                ///< JavaScript 파일 목록
        std::vector<std::string> css;               ///< CSS 파일 목록
        std::string run_at{"document_idle"};        ///< 실행 시점
        bool all_frames{false};                     ///< 모든 프레임에 삽입
        bool match_about_blank{false};              ///< about:blank에도 삽입
    };
    std::vector<ContentScriptDef> content_scripts;

    /// 팝업 UI 경로
    std::string popup_html;

    /// 옵션 페이지 경로
    std::string options_page;

    /// 웹 접근 가능 리소스
    struct WebAccessibleResource {
        std::vector<std::string> resources;
        std::vector<std::string> matches;
    };
    std::vector<WebAccessibleResource> web_accessible_resources;

    /// 콘텐츠 보안 정책
    std::string content_security_policy;

    /// 유효성 검사
    [[nodiscard]] bool isValid() const;
    [[nodiscard]] std::vector<std::string> validate() const;
};

/**
 * @brief 메시지 패싱 메시지 구조
 */
struct ExtensionMessage {
    std::string extension_id;           ///< 발신 확장 ID
    std::string type;                   ///< 메시지 타입 (내부 라우팅용)
    std::string data;                   ///< JSON 직렬화된 메시지 데이터
    std::string sender_context;         ///< 발신 컨텍스트 ("background", "content", "popup")
    int sender_tab_id{-1};              ///< 발신 탭 ID (-1이면 백그라운드)
    std::string sender_frame_url;       ///< 발신 프레임 URL
    uint64_t message_id{0};             ///< 고유 메시지 ID

    /// 타임스탬프
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

/**
 * @brief 메시지 수신 콜백
 */
using MessageCallback = std::function<void(const ExtensionMessage& message, 
                                            std::function<void(const std::string&)> sendResponse)>;

/**
 * @brief 확장 스토리지 영역 타입
 */
enum class StorageArea {
    Local,      ///< 로컬 스토리지 (디바이스 한정)
    Sync,       ///< 동기화 스토리지 (계정 간 동기)
    Session     ///< 세션 스토리지 (브라우저 종료 시 삭제)
};

/**
 * @brief 확장 스토리지 변경 이벤트
 */
struct StorageChange {
    std::string key;
    std::optional<std::string> old_value;   ///< 이전 값 (JSON)
    std::optional<std::string> new_value;   ///< 새 값 (JSON)
};

using StorageChangeCallback = std::function<void(const std::vector<StorageChange>&, StorageArea)>;

/**
 * @brief 확장 프로그램 클래스
 * 
 * 하나의 확장 프로그램 인스턴스를 나타냅니다.
 * 각 확장은 독립된 V8 Isolate에서 실행되어 샌드박싱됩니다.
 */
class Extension {
public:
    /**
     * @brief 확장 ID로 생성 (디렉토리 기반)
     * @param id 확장 고유 ID
     * @param base_path 확장 파일 기본 경로
     */
    explicit Extension(const std::string& id, const std::string& base_path);
    ~Extension();

    // 이동만 허용
    Extension(Extension&& other) noexcept;
    Extension& operator=(Extension&& other) noexcept;
    Extension(const Extension&) = delete;
    Extension& operator=(const Extension&) = delete;

    // ============================
    // 기본 정보
    // ============================

    [[nodiscard]] const std::string& id() const { return id_; }
    [[nodiscard]] const std::string& basePath() const { return base_path_; }
    [[nodiscard]] const ExtensionManifest& manifest() const { return manifest_; }
    [[nodiscard]] ExtensionState state() const { return state_.load(); }
    [[nodiscard]] std::string stateString() const;

    // ============================
    // 수명 주기
    // ============================

    /**
     * @brief 매니페스트 로드 및 파싱
     * @return 성공 여부와 에러 메시지
     */
    [[nodiscard]] std::pair<bool, std::string> loadManifest();

    /**
     * @brief 확장 활성화 (V8 Isolate 생성, 스크립트 로드)
     * @return 성공 여부
     */
    bool activate();

    /**
     * @brief 확장 비활성화 (V8 Isolate 해제)
     */
    void deactivate();

    /**
     * @brief 확장 리로드 (개발 모드)
     * @return 성공 여부
     */
    bool reload();

    /**
     * @brief 일시 중단 (메모리 절약)
     */
    void suspend();

    /**
     * @brief 일시 중단 해제
     */
    void resume();

    // ============================
    // 권한 관리
    // ============================

    /**
     * @brief 특정 권한 보유 여부 확인
     */
    [[nodiscard]] bool hasPermission(Permission perm) const;

    /**
     * @brief 호스트 권한 확인 (URL 패턴 매칭)
     */
    [[nodiscard]] bool hasHostPermission(const std::string& url) const;

    /**
     * @brief 선택적 권한 부여
     * @return 성공 여부
     */
    bool grantPermission(Permission perm);

    /**
     * @brief 선택적 권한 회수
     */
    void revokePermission(Permission perm);

    /**
     * @brief 부여된 모든 권한 목록
     */
    [[nodiscard]] std::unordered_set<Permission> grantedPermissions() const;

    // ============================
    // 메시지 패싱
    // ============================

    /**
     * @brief 확장 내부 메시지 전송 (background ↔ content)
     * @param message 전송할 메시지
     * @param responseCallback 응답 콜백 (선택적)
     */
    void sendMessage(const ExtensionMessage& message,
                     std::function<void(const std::string&)> responseCallback = nullptr);

    /**
     * @brief 다른 확장에게 외부 메시지 전송
     * @param target_extension_id 대상 확장 ID
     * @param message 전송할 메시지
     * @param responseCallback 응답 콜백 (선택적)
     */
    void sendExternalMessage(const std::string& target_extension_id,
                             const ExtensionMessage& message,
                             std::function<void(const std::string&)> responseCallback = nullptr);

    /**
     * @brief 메시지 수신 리스너 등록
     * @param callback 메시지 수신 콜백
     */
    void onMessage(MessageCallback callback);

    /**
     * @brief 외부 메시지 수신 리스너 등록
     * @param callback 외부 메시지 수신 콜백
     */
    void onExternalMessage(MessageCallback callback);

    /**
     * @brief 수신된 메시지 처리 (ExtensionManager에서 호출)
     */
    void handleIncomingMessage(const ExtensionMessage& message,
                               std::function<void(const std::string&)> sendResponse);

    // ============================
    // 스토리지 API
    // ============================

    /**
     * @brief 스토리지에서 값 가져오기
     * @param area 스토리지 영역
     * @param keys 키 목록
     * @return 키-값 맵 (JSON 문자열)
     */
    [[nodiscard]] std::unordered_map<std::string, std::string> storageGet(
        StorageArea area,
        const std::vector<std::string>& keys
    );

    /**
     * @brief 스토리지에 값 저장
     * @param area 스토리지 영역
     * @param items 키-값 맵 (JSON 문자열)
     */
    void storageSet(StorageArea area,
                    const std::unordered_map<std::string, std::string>& items);

    /**
     * @brief 스토리지에서 값 삭제
     * @param area 스토리지 영역
     * @param keys 삭제할 키 목록
     */
    void storageRemove(StorageArea area, const std::vector<std::string>& keys);

    /**
     * @brief 스토리지 전체 삭제
     * @param area 스토리지 영역
     */
    void storageClear(StorageArea area);

    /**
     * @brief 스토리지 사용량 조회 (바이트)
     */
    [[nodiscard]] size_t storageUsage(StorageArea area) const;

    /**
     * @brief 스토리지 변경 리스너 등록
     */
    void onStorageChanged(StorageChangeCallback callback);

    // ============================
    // 콘텐츠 스크립트 관리
    // ============================

    /**
     * @brief 등록된 콘텐츠 스크립트 목록
     */
    [[nodiscard]] const std::vector<std::shared_ptr<ContentScript>>& contentScripts() const;

    /**
     * @brief 특정 URL에 삽입할 콘텐츠 스크립트 필터링
     */
    [[nodiscard]] std::vector<std::shared_ptr<ContentScript>> contentScriptsForUrl(
        const std::string& url
    ) const;

    // ============================
    // 팝업 UI
    // ============================

    /**
     * @brief 팝업 HTML 경로 반환
     */
    [[nodiscard]] std::optional<std::string> popupHtmlPath() const;

    /**
     * @brief 팝업 HTML 내용 반환
     */
    [[nodiscard]] std::optional<std::string> popupHtmlContent() const;

    // ============================
    // 리소스 접근
    // ============================

    /**
     * @brief 확장 내 파일 읽기
     * @param relative_path 확장 기준 상대 경로
     * @return 파일 내용 (없으면 nullopt)
     */
    [[nodiscard]] std::optional<std::string> readFile(const std::string& relative_path) const;

    /**
     * @brief 웹에서 접근 가능한 리소스인지 확인
     * @param resource_path 리소스 경로
     * @param page_url 요청 페이지 URL
     */
    [[nodiscard]] bool isWebAccessibleResource(const std::string& resource_path,
                                                const std::string& page_url) const;

    // ============================
    // 에러 / 로깅
    // ============================

    /**
     * @brief 마지막 에러 메시지
     */
    [[nodiscard]] const std::string& lastError() const { return last_error_; }

    /**
     * @brief 확장 로그 목록
     */
    [[nodiscard]] const std::vector<std::string>& logs() const { return logs_; }

    /**
     * @brief 로그 추가
     */
    void appendLog(const std::string& message);

private:
    std::string id_;                    ///< 확장 고유 ID
    std::string base_path_;             ///< 확장 파일 기본 경로
    ExtensionManifest manifest_;        ///< 매니페스트 데이터

    std::atomic<ExtensionState> state_{ExtensionState::Unloaded};
    std::string last_error_;

    /// 부여된 선택적 권한
    std::unordered_set<Permission> granted_optional_permissions_;
    mutable std::mutex permission_mutex_;

    /// 콘텐츠 스크립트
    std::vector<std::shared_ptr<ContentScript>> content_scripts_;

    /// 메시지 리스너
    std::vector<MessageCallback> message_listeners_;
    std::vector<MessageCallback> external_message_listeners_;
    mutable std::mutex message_mutex_;
    std::atomic<uint64_t> next_message_id_{1};

    /// 스토리지 (영역별 키-값 저장)
    std::unordered_map<StorageArea, std::unordered_map<std::string, std::string>> storage_data_;
    std::vector<StorageChangeCallback> storage_change_callbacks_;
    mutable std::mutex storage_mutex_;

    /// 로그
    std::vector<std::string> logs_;
    mutable std::mutex log_mutex_;

    // 내부 헬퍼
    bool loadContentScripts();
    bool initializeBackgroundScript();
    void notifyStorageChange(const std::vector<StorageChange>& changes, StorageArea area);
    std::string readFileInternal(const std::string& full_path) const;
};

} // namespace ordinal::extensions
