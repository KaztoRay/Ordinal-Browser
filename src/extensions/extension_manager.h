#pragma once

/**
 * @file extension_manager.h
 * @brief 확장 프로그램 관리자
 * 
 * 확장 프로그램의 로드/언로드/활성화/비활성화를 관리하고,
 * 매니페스트 파싱, 확장 수명 주기, 권한 시스템, V8 Isolate 샌드박싱을 제공합니다.
 */

#include "extension.h"

#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <filesystem>
#include <optional>

namespace ordinal::extensions {

/**
 * @brief 확장 설치 소스 타입
 */
enum class InstallSource {
    Local,          ///< 로컬 디렉토리에서 설치
    Archive,        ///< .crx/.zip 아카이브에서 설치
    WebStore,       ///< 웹 스토어에서 설치 (미래 기능)
    Development     ///< 개발 모드 (압축 해제된 확장 로드)
};

/**
 * @brief 확장 설치 결과
 */
struct InstallResult {
    bool success{false};
    std::string extension_id;           ///< 설치된 확장 ID
    std::string error_message;          ///< 실패 시 에러 메시지
    std::vector<Permission> dangerous_permissions;  ///< 위험 권한 목록 (사용자 확인 필요)
};

/**
 * @brief 확장 이벤트 타입
 */
enum class ExtensionEvent {
    Installed,      ///< 새 확장 설치됨
    Uninstalled,    ///< 확장 삭제됨
    Enabled,        ///< 확장 활성화됨
    Disabled,       ///< 확장 비활성화됨
    Updated,        ///< 확장 업데이트됨
    Error           ///< 오류 발생
};

/**
 * @brief 확장 이벤트 콜백
 */
using ExtensionEventCallback = std::function<void(ExtensionEvent event, const std::string& extension_id, 
                                                   const std::string& details)>;

/**
 * @brief 확장 관리자 설정
 */
struct ExtensionManagerConfig {
    std::string extensions_dir;                 ///< 확장 설치 디렉토리
    size_t max_extensions{100};                 ///< 최대 설치 가능 확장 수
    size_t max_memory_per_extension_mb{128};    ///< 확장당 최대 메모리 (MB)
    uint32_t script_timeout_ms{10000};          ///< 확장 스크립트 타임아웃 (밀리초)
    bool allow_developer_mode{true};            ///< 개발 모드 허용
    bool auto_update{true};                     ///< 자동 업데이트 활성화
    std::vector<std::string> blocked_extensions; ///< 차단된 확장 ID 목록
};

/**
 * @brief 확장 프로그램 관리자
 * 
 * 모든 확장 프로그램의 수명 주기를 관리하는 싱글톤 클래스입니다.
 * 확장 설치, 언로드, 활성화/비활성화, 매니페스트 검증, 권한 관리,
 * 확장 간 메시지 라우팅을 담당합니다.
 */
class ExtensionManager {
public:
    /**
     * @brief 싱글톤 인스턴스
     */
    static ExtensionManager& instance();

    ExtensionManager(const ExtensionManager&) = delete;
    ExtensionManager& operator=(const ExtensionManager&) = delete;

    /**
     * @brief 관리자 초기화
     * @param config 설정
     * @return 성공 여부
     */
    bool initialize(const ExtensionManagerConfig& config);

    /**
     * @brief 관리자 종료 (모든 확장 언로드)
     */
    void shutdown();

    /**
     * @brief 초기화 상태 확인
     */
    [[nodiscard]] bool isInitialized() const { return initialized_; }

    // ============================
    // 확장 설치 / 제거
    // ============================

    /**
     * @brief 로컬 디렉토리에서 확장 로드 (개발 모드)
     * @param directory_path 확장 디렉토리 경로
     * @return 설치 결과
     */
    [[nodiscard]] InstallResult loadUnpacked(const std::string& directory_path);

    /**
     * @brief 아카이브(.crx/.zip)에서 확장 설치
     * @param archive_path 아카이브 파일 경로
     * @return 설치 결과
     */
    [[nodiscard]] InstallResult installFromArchive(const std::string& archive_path);

    /**
     * @brief 확장 제거
     * @param extension_id 확장 ID
     * @param remove_data 확장 데이터도 삭제할지 여부
     * @return 성공 여부
     */
    bool uninstall(const std::string& extension_id, bool remove_data = true);

    // ============================
    // 확장 활성화 / 비활성화
    // ============================

    /**
     * @brief 확장 활성화
     * @param extension_id 확장 ID
     * @return 성공 여부
     */
    bool enable(const std::string& extension_id);

    /**
     * @brief 확장 비활성화
     * @param extension_id 확장 ID
     * @return 성공 여부
     */
    bool disable(const std::string& extension_id);

    /**
     * @brief 확장 리로드 (개발 모드)
     * @param extension_id 확장 ID
     * @return 성공 여부
     */
    bool reload(const std::string& extension_id);

    // ============================
    // 확장 조회
    // ============================

    /**
     * @brief 설치된 모든 확장 ID 목록
     */
    [[nodiscard]] std::vector<std::string> installedExtensionIds() const;

    /**
     * @brief 활성화된 확장 목록
     */
    [[nodiscard]] std::vector<std::string> enabledExtensionIds() const;

    /**
     * @brief ID로 확장 조회
     * @param extension_id 확장 ID
     * @return 확장 포인터 (없으면 nullptr)
     */
    [[nodiscard]] Extension* getExtension(const std::string& extension_id);
    [[nodiscard]] const Extension* getExtension(const std::string& extension_id) const;

    /**
     * @brief 확장 존재 여부 확인
     */
    [[nodiscard]] bool hasExtension(const std::string& extension_id) const;

    /**
     * @brief 확장 수
     */
    [[nodiscard]] size_t extensionCount() const;

    // ============================
    // URL에 대한 콘텐츠 스크립트 수집
    // ============================

    /**
     * @brief 특정 URL에 삽입할 모든 콘텐츠 스크립트 수집
     * @param url 페이지 URL
     * @return 콘텐츠 스크립트 목록 (확장 ID와 함께)
     */
    [[nodiscard]] std::vector<std::pair<std::string, std::shared_ptr<ContentScript>>> 
        contentScriptsForUrl(const std::string& url) const;

    // ============================
    // 확장 간 메시지 라우팅
    // ============================

    /**
     * @brief 확장 간 메시지 전달
     * @param from_extension_id 발신 확장 ID
     * @param to_extension_id 수신 확장 ID
     * @param message 메시지
     * @param responseCallback 응답 콜백
     */
    void routeMessage(const std::string& from_extension_id,
                      const std::string& to_extension_id,
                      const ExtensionMessage& message,
                      std::function<void(const std::string&)> responseCallback = nullptr);

    /**
     * @brief 모든 확장에 브로드캐스트 메시지 전송
     * @param from_extension_id 발신 확장 ID
     * @param message 메시지
     */
    void broadcastMessage(const std::string& from_extension_id,
                          const ExtensionMessage& message);

    // ============================
    // 권한 관리
    // ============================

    /**
     * @brief 확장의 선택적 권한 승인
     * @param extension_id 확장 ID
     * @param permission 승인할 권한
     * @return 성공 여부
     */
    bool approvePermission(const std::string& extension_id, Permission permission);

    /**
     * @brief 확장의 권한 회수
     * @param extension_id 확장 ID
     * @param permission 회수할 권한
     */
    void revokePermission(const std::string& extension_id, Permission permission);

    /**
     * @brief 확장의 위험 권한 목록 조회
     */
    [[nodiscard]] std::vector<Permission> dangerousPermissions(const std::string& extension_id) const;

    // ============================
    // 이벤트
    // ============================

    /**
     * @brief 확장 이벤트 리스너 등록
     */
    void addEventListener(ExtensionEventCallback callback);

    // ============================
    // V8 샌드박싱
    // ============================

    /**
     * @brief 확장의 V8 Isolate 메모리 사용량 조회
     */
    [[nodiscard]] size_t extensionMemoryUsage(const std::string& extension_id) const;

    /**
     * @brief 전체 확장의 총 메모리 사용량
     */
    [[nodiscard]] size_t totalMemoryUsage() const;

    /**
     * @brief 메모리 초과 확장 강제 일시 중단
     */
    void enforceMemoryLimits();

    // ============================
    // 유틸리티
    // ============================

    /**
     * @brief 확장 ID 생성 (디렉토리 이름 기반 해시)
     * @param path 확장 디렉토리 경로
     * @return 고유 확장 ID
     */
    [[nodiscard]] static std::string generateExtensionId(const std::string& path);

    /**
     * @brief 확장이 차단 목록에 있는지 확인
     */
    [[nodiscard]] bool isBlocked(const std::string& extension_id) const;

private:
    ExtensionManager() = default;
    ~ExtensionManager();

    /// 이벤트 발행
    void emitEvent(ExtensionEvent event, const std::string& extension_id, 
                   const std::string& details = "");

    /// 확장 디렉토리 스캔 및 자동 로드
    void scanExtensionsDirectory();

    /// 확장 상태 영속화 (어떤 확장이 활성/비활성인지)
    void saveExtensionStates();
    void loadExtensionStates();

    ExtensionManagerConfig config_;
    bool initialized_{false};

    /// 설치된 확장 맵 (ID → Extension)
    std::unordered_map<std::string, std::unique_ptr<Extension>> extensions_;
    mutable std::mutex extensions_mutex_;

    /// 이벤트 리스너
    std::vector<ExtensionEventCallback> event_listeners_;
    mutable std::mutex event_mutex_;

    /// 확장 메모리 사용량 캐시
    std::unordered_map<std::string, size_t> memory_usage_cache_;
};

} // namespace ordinal::extensions
