#pragma once

/**
 * @file download_manager.h
 * @brief 다운로드 관리자
 * 
 * HTTP/HTTPS 파일 다운로드, 진행률 추적, 일시정지/재개/취소,
 * 파일 명명, MIME 타입 처리, 다운로드 큐, SecurityAgent 바이러스 검사.
 */

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <cstdint>

namespace ordinal::data {

class DataStore;

// ============================================================
// 다운로드 상태
// ============================================================

/**
 * @brief 다운로드 상태
 */
enum class DownloadState {
    Queued,         ///< 큐 대기 중
    Downloading,    ///< 다운로드 중
    Paused,         ///< 일시정지
    Completed,      ///< 완료
    Failed,         ///< 실패
    Cancelled,      ///< 취소됨
    VirusDetected   ///< 바이러스 감지
};

/**
 * @brief MIME 타입 카테고리
 */
enum class MimeCategory {
    Unknown,        ///< 알 수 없음
    Document,       ///< 문서 (PDF, DOC 등)
    Image,          ///< 이미지
    Video,          ///< 비디오
    Audio,          ///< 오디오
    Archive,        ///< 압축 파일
    Executable,     ///< 실행 파일 (위험)
    Script,         ///< 스크립트 (위험)
    Other           ///< 기타
};

// ============================================================
// 다운로드 항목
// ============================================================

/**
 * @brief 다운로드 항목 정보
 */
struct DownloadItem {
    int64_t id{0};                      ///< 고유 ID
    std::string url;                    ///< 다운로드 URL
    std::string filename;               ///< 저장 파일명
    std::string save_path;              ///< 저장 경로 (전체)
    std::string mime_type;              ///< MIME 타입
    MimeCategory mime_category{MimeCategory::Unknown};

    DownloadState state{DownloadState::Queued};
    int64_t total_bytes{0};             ///< 전체 크기 (바이트)
    int64_t received_bytes{0};          ///< 수신 크기 (바이트)
    double speed_bps{0.0};              ///< 현재 속도 (바이트/초)
    int progress_percent{0};            ///< 진행률 (0-100)

    std::string error_message;          ///< 에러 메시지

    std::chrono::system_clock::time_point start_time;  ///< 시작 시간
    std::chrono::system_clock::time_point end_time;    ///< 완료 시간
    int64_t eta_seconds{-1};            ///< 예상 남은 시간 (초)

    std::string referrer_url;           ///< 참조 URL
    bool virus_scanned{false};          ///< 바이러스 검사 완료 여부
    bool virus_safe{true};              ///< 바이러스 검사 결과 (안전)
    std::string virus_info;             ///< 바이러스 정보
};

// ============================================================
// 콜백 타입
// ============================================================

/// 진행률 콜백: (download_id, received_bytes, total_bytes)
using DownloadProgressCallback = std::function<void(int64_t, int64_t, int64_t)>;

/// 상태 변경 콜백: (download_id, new_state)
using DownloadStateCallback = std::function<void(int64_t, DownloadState)>;

/// 완료 콜백: (download_id, save_path, success)
using DownloadCompleteCallback = std::function<void(int64_t, const std::string&, bool)>;

// ============================================================
// 다운로드 설정
// ============================================================

/**
 * @brief 다운로드 관리자 설정
 */
struct DownloadConfig {
    std::string default_download_dir;   ///< 기본 다운로드 디렉터리
    int max_concurrent{3};              ///< 최대 동시 다운로드 수
    int max_retries{3};                 ///< 최대 재시도 횟수
    int timeout_seconds{300};           ///< 다운로드 타임아웃 (초)
    bool auto_virus_scan{true};         ///< 자동 바이러스 검사
    bool auto_rename_duplicates{true};  ///< 중복 파일명 자동 변경
    bool show_save_dialog{false};       ///< 저장 위치 대화상자 표시
    int64_t max_file_size{0};           ///< 최대 파일 크기 (0이면 무제한)
    std::vector<std::string> blocked_extensions; ///< 차단할 확장자 목록
};

// ============================================================
// DownloadManager
// ============================================================

/**
 * @brief 다운로드 관리자
 * 
 * HTTP/HTTPS 파일 다운로드를 관리하며, 큐 기반 동시 다운로드,
 * 일시정지/재개, 바이러스 검사를 지원합니다.
 */
class DownloadManager {
public:
    explicit DownloadManager(std::shared_ptr<DataStore> store);
    ~DownloadManager();

    /**
     * @brief 초기화 (테이블 생성, 워커 스레드 시작)
     * @param config 다운로드 설정
     */
    bool initialize(const DownloadConfig& config = {});

    /**
     * @brief 종료 (워커 스레드 정지)
     */
    void shutdown();

    // ============================
    // 다운로드 시작/제어
    // ============================

    /**
     * @brief 다운로드 시작 (큐에 추가)
     * @param url 다운로드 URL
     * @param save_path 저장 경로 (비어있으면 기본 디렉터리 + 자동 파일명)
     * @param referrer 참조 URL
     * @return 다운로드 ID
     */
    int64_t startDownload(const std::string& url,
                          const std::string& save_path = "",
                          const std::string& referrer = "");

    /**
     * @brief 다운로드 일시정지
     */
    bool pauseDownload(int64_t download_id);

    /**
     * @brief 다운로드 재개
     */
    bool resumeDownload(int64_t download_id);

    /**
     * @brief 다운로드 취소
     */
    bool cancelDownload(int64_t download_id);

    /**
     * @brief 다운로드 재시도
     */
    bool retryDownload(int64_t download_id);

    /**
     * @brief 전체 일시정지
     */
    void pauseAll();

    /**
     * @brief 전체 재개
     */
    void resumeAll();

    // ============================
    // 조회
    // ============================

    /**
     * @brief 다운로드 항목 조회
     */
    std::optional<DownloadItem> getDownload(int64_t download_id) const;

    /**
     * @brief 활성 다운로드 목록
     */
    std::vector<DownloadItem> getActiveDownloads() const;

    /**
     * @brief 전체 다운로드 기록
     * @param limit 최대 수
     */
    std::vector<DownloadItem> getHistory(int limit = 100) const;

    /**
     * @brief 상태별 다운로드 조회
     */
    std::vector<DownloadItem> getByState(DownloadState state) const;

    // ============================
    // 기록 관리
    // ============================

    /**
     * @brief 다운로드 기록 삭제 (파일 유지)
     */
    bool removeFromHistory(int64_t download_id);

    /**
     * @brief 다운로드 기록 + 파일 삭제
     */
    bool deleteDownload(int64_t download_id);

    /**
     * @brief 완료된 다운로드 기록 전부 삭제
     */
    int clearCompletedHistory();

    // ============================
    // 콜백 등록
    // ============================

    /**
     * @brief 진행률 콜백 설정
     */
    void setProgressCallback(DownloadProgressCallback callback);

    /**
     * @brief 상태 변경 콜백 설정
     */
    void setStateCallback(DownloadStateCallback callback);

    /**
     * @brief 완료 콜백 설정
     */
    void setCompleteCallback(DownloadCompleteCallback callback);

    // ============================
    // 유틸리티
    // ============================

    /**
     * @brief URL에서 파일명 추출
     * @param url URL
     * @param content_disposition Content-Disposition 헤더 값
     */
    static std::string extractFilename(const std::string& url,
                                        const std::string& content_disposition = "");

    /**
     * @brief MIME 타입에서 카테고리 판별
     */
    static MimeCategory categorize(const std::string& mime_type);

    /**
     * @brief 파일 확장자에서 MIME 타입 추정
     */
    static std::string guessMimeType(const std::string& filename);

    /**
     * @brief 바이트 → 사람이 읽을 수 있는 형태
     */
    static std::string formatSize(int64_t bytes);

    /**
     * @brief 기본 다운로드 디렉터리 경로
     */
    [[nodiscard]] std::string defaultDownloadDir() const;

private:
    std::shared_ptr<DataStore> store_;
    DownloadConfig config_;
    mutable std::mutex mutex_;

    // 다운로드 큐 및 활성 목록
    std::queue<int64_t> download_queue_;                        ///< 대기열
    std::unordered_map<int64_t, DownloadItem> active_downloads_;///< 활성 다운로드
    std::atomic<int> active_count_{0};                          ///< 현재 활성 수

    // 워커 스레드
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
    std::condition_variable queue_cv_;

    // 콜백
    DownloadProgressCallback progress_callback_;
    DownloadStateCallback state_callback_;
    DownloadCompleteCallback complete_callback_;

    /**
     * @brief 워커 스레드 루프
     */
    void workerLoop();

    /**
     * @brief 실제 HTTP 다운로드 수행 (libcurl)
     * @param item 다운로드 항목
     */
    void performDownload(DownloadItem& item);

    /**
     * @brief SecurityAgent 바이러스 검사 호출
     */
    bool scanForVirus(DownloadItem& item);

    /**
     * @brief 다운로드 상태를 DB에 저장
     */
    void persistState(const DownloadItem& item);

    /**
     * @brief 다운로드 상태 업데이트 + 콜백 호출
     */
    void updateState(int64_t id, DownloadState new_state);

    /**
     * @brief 중복 파일명 해결 (파일명 뒤에 (1), (2) 등 추가)
     */
    std::string resolveFilename(const std::string& path) const;

    /**
     * @brief 확장자 차단 여부 확인
     */
    bool isBlockedExtension(const std::string& filename) const;

    /**
     * @brief DB 행 → DownloadItem 변환
     */
    DownloadItem rowToItem(
        const std::unordered_map<std::string,
            std::variant<std::nullptr_t, int64_t, double, std::string, std::vector<uint8_t>>>& row) const;
};

} // namespace ordinal::data
