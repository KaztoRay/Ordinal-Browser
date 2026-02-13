/**
 * @file download_manager.cpp
 * @brief 다운로드 관리자 구현
 *
 * libcurl 기반 HTTP/HTTPS 다운로드, Range 요청을 통한 일시정지/재개,
 * 워커 스레드 풀, 큐 기반 동시 다운로드, Content-Disposition 파일명 추출,
 * SecurityAgent 바이러스 검사, DataStore 영구 저장.
 */

#include "download_manager.h"
#include "data_store.h"

#include <curl/curl.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;
using Clock = std::chrono::system_clock;

namespace ordinal::data {

// ============================================================
// libcurl 콜백 (파일 기반)
// ============================================================

namespace {

/// libcurl 쓰기 콜백 — 파일에 저장
size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb,
                          void* userdata) {
    auto* ofs = static_cast<std::ofstream*>(userdata);
    if (!ofs || !ofs->is_open()) return 0;
    size_t bytes = size * nmemb;
    ofs->write(ptr, static_cast<std::streamsize>(bytes));
    return bytes;
}

/// libcurl 헤더 콜백 — Content-Disposition, Content-Type, Content-Length 캡처
struct HeaderCapture {
    std::string content_disposition;
    std::string content_type;
    int64_t content_length{-1};
    bool accept_ranges{false};
};

size_t curlHeaderCallback(char* buffer, size_t size, size_t nitems,
                           void* userdata) {
    auto* capture = static_cast<HeaderCapture*>(userdata);
    size_t total = size * nitems;
    std::string line(buffer, total);

    // 소문자 변환 비교용
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.starts_with("content-disposition:")) {
        capture->content_disposition = line.substr(20);
        // 양쪽 공백/개행 제거
        auto& s = capture->content_disposition;
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    } else if (lower.starts_with("content-type:")) {
        capture->content_type = line.substr(14);
        auto& s = capture->content_type;
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
        // 세미콜론 이후 파라미터 제거
        auto semi = s.find(';');
        if (semi != std::string::npos) s.resize(semi);
    } else if (lower.starts_with("content-length:")) {
        try {
            capture->content_length = std::stoll(line.substr(16));
        } catch (...) {}
    } else if (lower.starts_with("accept-ranges:")) {
        std::string val = lower.substr(15);
        val.erase(0, val.find_first_not_of(" \t"));
        if (val.starts_with("bytes")) {
            capture->accept_ranges = true;
        }
    }

    return total;
}

/// libcurl 진행률 콜백 구조
struct ProgressData {
    DownloadItem* item{nullptr};
    DownloadManager* manager{nullptr};
    Clock::time_point last_report;
    int64_t last_bytes{0};
};

int curlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                          [[maybe_unused]] curl_off_t ultotal,
                          [[maybe_unused]] curl_off_t ulnow) {
    auto* pd = static_cast<ProgressData*>(clientp);
    if (!pd || !pd->item) return 0;

    auto& item = *pd->item;

    // 취소/일시정지 체크 — 비영(0)이 아닌 값을 반환하면 전송 중단
    if (item.state == DownloadState::Cancelled ||
        item.state == DownloadState::Paused) {
        return 1; // 전송 중단 요청
    }

    // 바이트 업데이트
    item.received_bytes = item.total_bytes > 0
        ? (item.total_bytes - dltotal + dlnow) // 재개 시 오프셋 고려
        : dlnow;
    if (dltotal > 0) {
        item.total_bytes = item.total_bytes > 0
            ? item.total_bytes  // 이미 알고 있으면 유지
            : dltotal;
    }

    // 진행률 계산
    if (item.total_bytes > 0) {
        item.progress_percent = static_cast<int>(
            (item.received_bytes * 100) / item.total_bytes);
        item.progress_percent = std::clamp(item.progress_percent, 0, 100);
    }

    // 속도 계산 (500ms마다)
    auto now = Clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - pd->last_report).count();

    if (elapsed_ms >= 500) {
        int64_t delta = dlnow - pd->last_bytes;
        double seconds = static_cast<double>(elapsed_ms) / 1000.0;
        item.speed_bps = (seconds > 0.0) ? (static_cast<double>(delta) / seconds) : 0.0;

        // ETA 계산
        if (item.speed_bps > 0.0 && item.total_bytes > 0) {
            double remaining = static_cast<double>(item.total_bytes - item.received_bytes);
            item.eta_seconds = static_cast<int64_t>(remaining / item.speed_bps);
        } else {
            item.eta_seconds = -1;
        }

        pd->last_bytes = dlnow;
        pd->last_report = now;
    }

    return 0; // 계속 다운로드
}

/// 현재 시간 → ISO8601 문자열
std::string timeToString(Clock::time_point tp) {
    auto t = Clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

/// ISO8601 문자열 → time_point
Clock::time_point stringToTime(const std::string& s) {
    if (s.empty()) return Clock::now();
    std::tm tm{};
    strptime(s.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return Clock::from_time_t(timegm(&tm));
}

} // 익명 네임스페이스

// ============================================================
// 생성자 / 소멸자
// ============================================================

DownloadManager::DownloadManager(std::shared_ptr<DataStore> store)
    : store_(std::move(store)) {}

DownloadManager::~DownloadManager() {
    shutdown();
}

// ============================================================
// 초기화 / 종료
// ============================================================

bool DownloadManager::initialize(const DownloadConfig& config) {
    if (!store_ || !store_->isOpen()) return false;

    config_ = config;

    // 기본 다운로드 디렉터리 설정
    if (config_.default_download_dir.empty()) {
        auto home = fs::path(std::getenv("HOME") ? std::getenv("HOME") : "/tmp");
        config_.default_download_dir = (home / "Downloads").string();
    }
    fs::create_directories(config_.default_download_dir);

    // 마이그레이션 — 다운로드 테이블
    Migration mig;
    mig.version = 300;
    mig.name = "다운로드 테이블 생성";
    mig.up_sql = R"SQL(
        CREATE TABLE IF NOT EXISTS downloads (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            url             TEXT NOT NULL,
            filename        TEXT NOT NULL DEFAULT '',
            save_path       TEXT NOT NULL DEFAULT '',
            mime_type       TEXT DEFAULT '',
            mime_category   INTEGER DEFAULT 0,
            state           INTEGER DEFAULT 0,
            total_bytes     INTEGER DEFAULT 0,
            received_bytes  INTEGER DEFAULT 0,
            error_message   TEXT DEFAULT '',
            start_time      TEXT DEFAULT '',
            end_time        TEXT DEFAULT '',
            referrer_url    TEXT DEFAULT '',
            virus_scanned   INTEGER DEFAULT 0,
            virus_safe      INTEGER DEFAULT 1,
            virus_info      TEXT DEFAULT ''
        );
        CREATE INDEX IF NOT EXISTS idx_downloads_state ON downloads(state);
        CREATE INDEX IF NOT EXISTS idx_downloads_start ON downloads(start_time DESC);
    )SQL";
    mig.down_sql = "DROP TABLE IF EXISTS downloads;";

    store_->registerMigration(mig);
    store_->runMigrations();

    // libcurl 글로벌 초기화
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // 워커 스레드 시작
    running_ = true;
    int num_workers = std::max(1, config_.max_concurrent);
    for (int i = 0; i < num_workers; ++i) {
        workers_.emplace_back(&DownloadManager::workerLoop, this);
    }

    return true;
}

void DownloadManager::shutdown() {
    // 모든 활성 다운로드 일시정지 처리
    {
        std::lock_guard lock(mutex_);
        for (auto& [id, item] : active_downloads_) {
            if (item.state == DownloadState::Downloading) {
                item.state = DownloadState::Paused;
            }
        }
    }

    // 워커 스레드 종료 신호
    running_ = false;
    queue_cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();

    // libcurl 정리
    curl_global_cleanup();
}

// ============================================================
// 다운로드 시작/제어
// ============================================================

int64_t DownloadManager::startDownload(const std::string& url,
                                        const std::string& save_path,
                                        const std::string& referrer) {
    // 확장자 차단 확인
    std::string filename = extractFilename(url);
    if (isBlockedExtension(filename)) {
        return -1; // 차단됨
    }

    // DownloadItem 생성
    DownloadItem item;
    item.url = url;
    item.referrer_url = referrer;
    item.state = DownloadState::Queued;
    item.start_time = Clock::now();

    // 저장 경로 결정
    if (!save_path.empty()) {
        item.save_path = save_path;
        item.filename = fs::path(save_path).filename().string();
    } else {
        item.filename = filename;
        item.save_path = (fs::path(config_.default_download_dir) / item.filename).string();
    }

    // 중복 파일명 해결
    if (config_.auto_rename_duplicates) {
        item.save_path = resolveFilename(item.save_path);
        item.filename = fs::path(item.save_path).filename().string();
    }

    // DB 저장
    store_->execute(
        "INSERT INTO downloads (url, filename, save_path, mime_type, mime_category, "
        "state, total_bytes, received_bytes, error_message, start_time, end_time, "
        "referrer_url, virus_scanned, virus_safe, virus_info) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        {
            item.url, item.filename, item.save_path, item.mime_type,
            static_cast<int64_t>(item.mime_category),
            static_cast<int64_t>(item.state),
            item.total_bytes, item.received_bytes, item.error_message,
            timeToString(item.start_time), std::string{},
            item.referrer_url,
            static_cast<int64_t>(item.virus_scanned ? 1 : 0),
            static_cast<int64_t>(item.virus_safe ? 1 : 0),
            item.virus_info
        }
    );
    item.id = store_->lastInsertRowId();

    // 큐에 추가
    {
        std::lock_guard lock(mutex_);
        active_downloads_[item.id] = item;
        download_queue_.push(item.id);
    }
    queue_cv_.notify_one();

    // 상태 콜백
    if (state_callback_) {
        state_callback_(item.id, DownloadState::Queued);
    }

    return item.id;
}

bool DownloadManager::pauseDownload(int64_t download_id) {
    std::lock_guard lock(mutex_);
    auto it = active_downloads_.find(download_id);
    if (it == active_downloads_.end()) return false;

    if (it->second.state != DownloadState::Downloading &&
        it->second.state != DownloadState::Queued) {
        return false;
    }

    it->second.state = DownloadState::Paused;
    persistState(it->second);

    if (state_callback_) {
        state_callback_(download_id, DownloadState::Paused);
    }
    return true;
}

bool DownloadManager::resumeDownload(int64_t download_id) {
    std::lock_guard lock(mutex_);
    auto it = active_downloads_.find(download_id);
    if (it == active_downloads_.end()) {
        // DB에서 복원 시도
        auto rows = store_->query(
            "SELECT * FROM downloads WHERE id = ? AND state = ?",
            {download_id, static_cast<int64_t>(DownloadState::Paused)}
        );
        if (rows.empty()) return false;

        auto item = rowToItem(rows.front());
        item.state = DownloadState::Queued;
        active_downloads_[item.id] = item;
        download_queue_.push(item.id);
        queue_cv_.notify_one();
        return true;
    }

    if (it->second.state != DownloadState::Paused) return false;

    it->second.state = DownloadState::Queued;
    download_queue_.push(download_id);
    queue_cv_.notify_one();

    if (state_callback_) {
        state_callback_(download_id, DownloadState::Queued);
    }
    return true;
}

bool DownloadManager::cancelDownload(int64_t download_id) {
    std::lock_guard lock(mutex_);
    auto it = active_downloads_.find(download_id);
    if (it == active_downloads_.end()) return false;

    it->second.state = DownloadState::Cancelled;
    it->second.end_time = Clock::now();
    persistState(it->second);

    // 부분 파일 삭제
    std::error_code ec;
    fs::remove(it->second.save_path, ec);
    fs::remove(it->second.save_path + ".part", ec);

    if (state_callback_) {
        state_callback_(download_id, DownloadState::Cancelled);
    }

    active_downloads_.erase(it);
    return true;
}

bool DownloadManager::retryDownload(int64_t download_id) {
    std::lock_guard lock(mutex_);
    auto it = active_downloads_.find(download_id);
    if (it == active_downloads_.end()) {
        // DB에서 실패한 항목 복원
        auto rows = store_->query(
            "SELECT * FROM downloads WHERE id = ? AND state IN (?, ?)",
            {download_id,
             static_cast<int64_t>(DownloadState::Failed),
             static_cast<int64_t>(DownloadState::Cancelled)}
        );
        if (rows.empty()) return false;

        auto item = rowToItem(rows.front());
        item.state = DownloadState::Queued;
        item.received_bytes = 0;
        item.error_message.clear();
        active_downloads_[item.id] = item;
        download_queue_.push(item.id);
        queue_cv_.notify_one();
        return true;
    }

    if (it->second.state != DownloadState::Failed &&
        it->second.state != DownloadState::Cancelled) {
        return false;
    }

    it->second.state = DownloadState::Queued;
    it->second.received_bytes = 0;
    it->second.error_message.clear();
    download_queue_.push(download_id);
    queue_cv_.notify_one();
    return true;
}

void DownloadManager::pauseAll() {
    std::lock_guard lock(mutex_);
    for (auto& [id, item] : active_downloads_) {
        if (item.state == DownloadState::Downloading ||
            item.state == DownloadState::Queued) {
            item.state = DownloadState::Paused;
            persistState(item);
            if (state_callback_) {
                state_callback_(id, DownloadState::Paused);
            }
        }
    }
}

void DownloadManager::resumeAll() {
    std::lock_guard lock(mutex_);
    for (auto& [id, item] : active_downloads_) {
        if (item.state == DownloadState::Paused) {
            item.state = DownloadState::Queued;
            download_queue_.push(id);
        }
    }
    queue_cv_.notify_all();
}

// ============================================================
// 조회
// ============================================================

std::optional<DownloadItem> DownloadManager::getDownload(int64_t download_id) const {
    std::lock_guard lock(mutex_);

    // 먼저 활성 목록에서 찾기 (실시간 데이터)
    auto it = active_downloads_.find(download_id);
    if (it != active_downloads_.end()) {
        return it->second;
    }

    // DB에서 조회
    auto rows = store_->query("SELECT * FROM downloads WHERE id = ?",
                               {download_id});
    if (rows.empty()) return std::nullopt;
    return rowToItem(rows.front());
}

std::vector<DownloadItem> DownloadManager::getActiveDownloads() const {
    std::lock_guard lock(mutex_);
    std::vector<DownloadItem> result;
    result.reserve(active_downloads_.size());
    for (const auto& [id, item] : active_downloads_) {
        if (item.state == DownloadState::Downloading ||
            item.state == DownloadState::Queued ||
            item.state == DownloadState::Paused) {
            result.push_back(item);
        }
    }
    // 시작 시간 내림차순 정렬
    std::sort(result.begin(), result.end(),
              [](const DownloadItem& a, const DownloadItem& b) {
                  return a.start_time > b.start_time;
              });
    return result;
}

std::vector<DownloadItem> DownloadManager::getHistory(int limit) const {
    auto rows = store_->query(
        "SELECT * FROM downloads ORDER BY start_time DESC LIMIT ?",
        {static_cast<int64_t>(limit)}
    );
    std::vector<DownloadItem> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.push_back(rowToItem(row));
    }
    return result;
}

std::vector<DownloadItem> DownloadManager::getByState(DownloadState state) const {
    auto rows = store_->query(
        "SELECT * FROM downloads WHERE state = ? ORDER BY start_time DESC",
        {static_cast<int64_t>(state)}
    );
    std::vector<DownloadItem> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.push_back(rowToItem(row));
    }
    return result;
}

// ============================================================
// 기록 관리
// ============================================================

bool DownloadManager::removeFromHistory(int64_t download_id) {
    {
        std::lock_guard lock(mutex_);
        // 활성이면 먼저 취소
        auto it = active_downloads_.find(download_id);
        if (it != active_downloads_.end()) {
            if (it->second.state == DownloadState::Downloading) {
                it->second.state = DownloadState::Cancelled;
            }
            active_downloads_.erase(it);
        }
    }
    return store_->execute("DELETE FROM downloads WHERE id = ?",
                            {download_id}) > 0;
}

bool DownloadManager::deleteDownload(int64_t download_id) {
    // 파일 경로 조회
    auto item = getDownload(download_id);
    if (!item) return false;

    // 파일 삭제
    std::error_code ec;
    fs::remove(item->save_path, ec);
    fs::remove(item->save_path + ".part", ec);

    return removeFromHistory(download_id);
}

int DownloadManager::clearCompletedHistory() {
    return store_->execute(
        "DELETE FROM downloads WHERE state IN (?, ?)",
        {static_cast<int64_t>(DownloadState::Completed),
         static_cast<int64_t>(DownloadState::Cancelled)}
    );
}

// ============================================================
// 콜백 등록
// ============================================================

void DownloadManager::setProgressCallback(DownloadProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

void DownloadManager::setStateCallback(DownloadStateCallback callback) {
    state_callback_ = std::move(callback);
}

void DownloadManager::setCompleteCallback(DownloadCompleteCallback callback) {
    complete_callback_ = std::move(callback);
}

// ============================================================
// 유틸리티 (정적)
// ============================================================

std::string DownloadManager::extractFilename(const std::string& url,
                                              const std::string& content_disposition) {
    // 1) Content-Disposition에서 filename 추출
    if (!content_disposition.empty()) {
        // filename*=UTF-8''... 형식 (RFC 5987)
        std::regex re_star(R"(filename\*\s*=\s*[Uu][Tt][Ff]-8'[^']*'([^;\s]+))",
                           std::regex::icase);
        std::smatch match;
        if (std::regex_search(content_disposition, match, re_star)) {
            std::string encoded = match[1].str();
            // URL 디코딩 (간이)
            std::string decoded;
            for (size_t i = 0; i < encoded.size(); ++i) {
                if (encoded[i] == '%' && i + 2 < encoded.size()) {
                    int hex = 0;
                    std::istringstream iss(encoded.substr(i + 1, 2));
                    iss >> std::hex >> hex;
                    decoded += static_cast<char>(hex);
                    i += 2;
                } else {
                    decoded += encoded[i];
                }
            }
            if (!decoded.empty()) return decoded;
        }

        // filename="..." 형식
        std::regex re_quoted(R"(filename\s*=\s*"([^"]+)")", std::regex::icase);
        if (std::regex_search(content_disposition, match, re_quoted)) {
            return match[1].str();
        }

        // filename=... (따옴표 없이)
        std::regex re_plain(R"(filename\s*=\s*([^;\s]+))", std::regex::icase);
        if (std::regex_search(content_disposition, match, re_plain)) {
            return match[1].str();
        }
    }

    // 2) URL 경로에서 추출
    std::string path = url;

    // 쿼리스트링 제거
    auto q = path.find('?');
    if (q != std::string::npos) path.resize(q);

    // 프래그먼트 제거
    auto h = path.find('#');
    if (h != std::string::npos) path.resize(h);

    // 마지막 슬래시 뒤의 문자열
    auto slash = path.rfind('/');
    if (slash != std::string::npos && slash + 1 < path.size()) {
        std::string name = path.substr(slash + 1);
        // URL 디코딩 (간이)
        std::string decoded;
        for (size_t i = 0; i < name.size(); ++i) {
            if (name[i] == '%' && i + 2 < name.size()) {
                int hex = 0;
                std::istringstream iss(name.substr(i + 1, 2));
                iss >> std::hex >> hex;
                decoded += static_cast<char>(hex);
                i += 2;
            } else if (name[i] == '+') {
                decoded += ' ';
            } else {
                decoded += name[i];
            }
        }
        if (!decoded.empty()) return decoded;
    }

    // 3) 기본 파일명
    return "download";
}

MimeCategory DownloadManager::categorize(const std::string& mime_type) {
    std::string lower = mime_type;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // 문서
    if (lower.find("pdf") != std::string::npos ||
        lower.find("document") != std::string::npos ||
        lower.find("spreadsheet") != std::string::npos ||
        lower.find("presentation") != std::string::npos ||
        lower.find("msword") != std::string::npos ||
        lower.find("text/plain") != std::string::npos ||
        lower.find("text/html") != std::string::npos ||
        lower.find("text/csv") != std::string::npos) {
        return MimeCategory::Document;
    }

    // 이미지
    if (lower.starts_with("image/")) {
        return MimeCategory::Image;
    }

    // 비디오
    if (lower.starts_with("video/")) {
        return MimeCategory::Video;
    }

    // 오디오
    if (lower.starts_with("audio/")) {
        return MimeCategory::Audio;
    }

    // 압축
    if (lower.find("zip") != std::string::npos ||
        lower.find("gzip") != std::string::npos ||
        lower.find("tar") != std::string::npos ||
        lower.find("rar") != std::string::npos ||
        lower.find("7z") != std::string::npos ||
        lower.find("compress") != std::string::npos) {
        return MimeCategory::Archive;
    }

    // 실행 파일
    if (lower.find("executable") != std::string::npos ||
        lower.find("x-msdownload") != std::string::npos ||
        lower.find("x-msdos") != std::string::npos ||
        lower.find("x-mach-binary") != std::string::npos) {
        return MimeCategory::Executable;
    }

    // 스크립트
    if (lower.find("javascript") != std::string::npos ||
        lower.find("x-python") != std::string::npos ||
        lower.find("x-perl") != std::string::npos ||
        lower.find("x-shellscript") != std::string::npos ||
        lower.find("x-sh") != std::string::npos) {
        return MimeCategory::Script;
    }

    if (lower.find("octet-stream") != std::string::npos) {
        return MimeCategory::Other;
    }

    return MimeCategory::Unknown;
}

std::string DownloadManager::guessMimeType(const std::string& filename) {
    auto ext = fs::path(filename).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // 확장자 → MIME 매핑 (주요)
    static const std::unordered_map<std::string, std::string> mime_map = {
        {".html", "text/html"}, {".htm", "text/html"},
        {".css",  "text/css"},  {".js", "application/javascript"},
        {".json", "application/json"}, {".xml", "application/xml"},
        {".txt",  "text/plain"}, {".csv", "text/csv"},
        {".pdf",  "application/pdf"},
        {".doc",  "application/msword"},
        {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".xls",  "application/vnd.ms-excel"},
        {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".ppt",  "application/vnd.ms-powerpoint"},
        {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {".png",  "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"}, {".webp", "image/webp"}, {".svg", "image/svg+xml"},
        {".ico",  "image/x-icon"}, {".bmp", "image/bmp"},
        {".mp4",  "video/mp4"}, {".webm", "video/webm"}, {".avi", "video/x-msvideo"},
        {".mkv",  "video/x-matroska"}, {".mov", "video/quicktime"},
        {".mp3",  "audio/mpeg"}, {".wav", "audio/wav"}, {".ogg", "audio/ogg"},
        {".flac", "audio/flac"}, {".aac", "audio/aac"},
        {".zip",  "application/zip"}, {".gz", "application/gzip"},
        {".tar",  "application/x-tar"}, {".rar", "application/vnd.rar"},
        {".7z",   "application/x-7z-compressed"},
        {".exe",  "application/x-msdownload"}, {".msi", "application/x-msi"},
        {".dmg",  "application/x-apple-diskimage"},
        {".deb",  "application/vnd.debian.binary-package"},
        {".rpm",  "application/x-rpm"},
        {".sh",   "application/x-sh"}, {".py", "text/x-python"},
        {".wasm", "application/wasm"},
    };

    auto it = mime_map.find(ext);
    if (it != mime_map.end()) return it->second;
    return "application/octet-stream";
}

std::string DownloadManager::formatSize(int64_t bytes) {
    if (bytes < 0) return "알 수 없음";

    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_idx < 4) {
        size /= 1024.0;
        ++unit_idx;
    }

    char buf[64];
    if (unit_idx == 0) {
        std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f %s", size, units[unit_idx]);
    }
    return buf;
}

std::string DownloadManager::defaultDownloadDir() const {
    return config_.default_download_dir;
}

// ============================================================
// 워커 스레드
// ============================================================

void DownloadManager::workerLoop() {
    while (running_) {
        int64_t download_id = -1;

        // 큐에서 다운로드 ID 가져오기
        {
            std::unique_lock lock(mutex_);
            queue_cv_.wait(lock, [this] {
                return !running_ || !download_queue_.empty();
            });

            if (!running_) break;
            if (download_queue_.empty()) continue;

            download_id = download_queue_.front();
            download_queue_.pop();
        }

        // 활성 다운로드에서 항목 참조
        DownloadItem item_copy;
        {
            std::lock_guard lock(mutex_);
            auto it = active_downloads_.find(download_id);
            if (it == active_downloads_.end()) continue;

            // 취소/완료되었으면 건너뛰기
            if (it->second.state == DownloadState::Cancelled ||
                it->second.state == DownloadState::Completed) {
                continue;
            }

            it->second.state = DownloadState::Downloading;
            ++active_count_;
            item_copy = it->second;
        }

        // 상태 콜백 (Downloading)
        if (state_callback_) {
            state_callback_(download_id, DownloadState::Downloading);
        }

        // 실제 다운로드 수행
        performDownload(item_copy);

        // 결과 업데이트
        {
            std::lock_guard lock(mutex_);
            auto it = active_downloads_.find(download_id);
            if (it != active_downloads_.end()) {
                it->second = item_copy;
            }
            --active_count_;
        }

        // 바이러스 검사 (완료 시)
        if (item_copy.state == DownloadState::Completed && config_.auto_virus_scan) {
            scanForVirus(item_copy);
            {
                std::lock_guard lock(mutex_);
                auto it = active_downloads_.find(download_id);
                if (it != active_downloads_.end()) {
                    it->second.virus_scanned = item_copy.virus_scanned;
                    it->second.virus_safe = item_copy.virus_safe;
                    it->second.virus_info = item_copy.virus_info;
                    if (!item_copy.virus_safe) {
                        it->second.state = DownloadState::VirusDetected;
                        item_copy.state = DownloadState::VirusDetected;
                    }
                }
            }
        }

        // 상태 저장
        persistState(item_copy);

        // 완료 콜백
        if (item_copy.state == DownloadState::Completed) {
            if (complete_callback_) {
                complete_callback_(download_id, item_copy.save_path, true);
            }
        } else if (item_copy.state == DownloadState::Failed) {
            if (complete_callback_) {
                complete_callback_(download_id, item_copy.save_path, false);
            }
        } else if (item_copy.state == DownloadState::VirusDetected) {
            if (state_callback_) {
                state_callback_(download_id, DownloadState::VirusDetected);
            }
        }

        // 일시정지 상태면 활성 목록에 유지 (큐에서는 제거됨)
        // 완료/실패/취소면 활성 목록에서 제거하지 않음 (UI에서 보여주기 위해)
    }
}

// ============================================================
// 실제 다운로드 수행 (libcurl)
// ============================================================

void DownloadManager::performDownload(DownloadItem& item) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        item.state = DownloadState::Failed;
        item.error_message = "libcurl 초기화 실패";
        item.end_time = Clock::now();
        return;
    }

    // 부분 파일 경로 (.part 접미사)
    std::string part_path = item.save_path + ".part";
    int64_t resume_offset = 0;

    // 기존 부분 파일이 있으면 이어받기
    if (fs::exists(part_path)) {
        resume_offset = static_cast<int64_t>(fs::file_size(part_path));
        item.received_bytes = resume_offset;
    }

    // 파일 열기 (이어쓰기 또는 새로 생성)
    std::ofstream ofs;
    if (resume_offset > 0) {
        ofs.open(part_path, std::ios::binary | std::ios::app);
    } else {
        ofs.open(part_path, std::ios::binary | std::ios::trunc);
    }

    if (!ofs.is_open()) {
        item.state = DownloadState::Failed;
        item.error_message = "파일 열기 실패: " + part_path;
        item.end_time = Clock::now();
        curl_easy_cleanup(curl);
        return;
    }

    // 헤더 캡처
    HeaderCapture headers;

    // 진행률 데이터
    ProgressData progress;
    progress.item = &item;
    progress.manager = this;
    progress.last_report = Clock::now();
    progress.last_bytes = 0;

    // libcurl 옵션 설정
    curl_easy_setopt(curl, CURLOPT_URL, item.url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlHeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(config_.timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    // User-Agent 설정
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "OrdinalBrowser/1.1.0 (libcurl)");

    // 참조 URL 설정
    if (!item.referrer_url.empty()) {
        curl_easy_setopt(curl, CURLOPT_REFERER, item.referrer_url.c_str());
    }

    // Range 요청으로 이어받기
    if (resume_offset > 0) {
        std::string range = std::to_string(resume_offset) + "-";
        curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
    }

    // 다운로드 수행
    CURLcode res = curl_easy_perform(curl);

    ofs.close();

    // 결과 처리
    if (res == CURLE_OK) {
        // HTTP 상태 코드 확인
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        if (http_code >= 200 && http_code < 300) {
            // 성공 — .part → 최종 파일로 이름 변경
            std::error_code ec;
            fs::rename(part_path, item.save_path, ec);
            if (ec) {
                // rename 실패 시 복사 후 삭제 시도
                fs::copy_file(part_path, item.save_path,
                              fs::copy_options::overwrite_existing, ec);
                fs::remove(part_path, ec);
            }

            item.state = DownloadState::Completed;
            item.end_time = Clock::now();
            item.progress_percent = 100;

            // MIME 정보 업데이트
            if (!headers.content_type.empty()) {
                item.mime_type = headers.content_type;
                item.mime_category = categorize(headers.content_type);
            } else {
                item.mime_type = guessMimeType(item.filename);
                item.mime_category = categorize(item.mime_type);
            }

            // Content-Disposition에서 실제 파일명 추출 후 이름 변경
            if (!headers.content_disposition.empty()) {
                std::string real_name = extractFilename(
                    item.url, headers.content_disposition);
                if (!real_name.empty() && real_name != item.filename) {
                    std::string new_path = (fs::path(item.save_path).parent_path()
                                            / real_name).string();
                    if (config_.auto_rename_duplicates) {
                        new_path = resolveFilename(new_path);
                    }
                    std::error_code ec2;
                    fs::rename(item.save_path, new_path, ec2);
                    if (!ec2) {
                        item.save_path = new_path;
                        item.filename = fs::path(new_path).filename().string();
                    }
                }
            }

            // 실제 파일 크기 확인
            std::error_code ec2;
            auto fsize = fs::file_size(item.save_path, ec2);
            if (!ec2) {
                item.total_bytes = static_cast<int64_t>(fsize);
                item.received_bytes = item.total_bytes;
            }
        } else if (http_code == 416) {
            // 416 Range Not Satisfiable — 이미 완료된 파일
            std::error_code ec;
            fs::rename(part_path, item.save_path, ec);
            item.state = DownloadState::Completed;
            item.end_time = Clock::now();
            item.progress_percent = 100;
        } else {
            // HTTP 오류
            item.state = DownloadState::Failed;
            item.error_message = "HTTP " + std::to_string(http_code);
            item.end_time = Clock::now();
        }
    } else if (res == CURLE_ABORTED_BY_CALLBACK) {
        // 사용자가 일시정지 또는 취소
        if (item.state == DownloadState::Paused) {
            // 부분 파일 유지 — 나중에 이어받기
        } else {
            // 취소 — 부분 파일 삭제
            std::error_code ec;
            fs::remove(part_path, ec);
        }
    } else {
        // 네트워크 오류
        item.state = DownloadState::Failed;
        item.error_message = std::string("cURL 오류: ") + curl_easy_strerror(res);
        item.end_time = Clock::now();
    }

    curl_easy_cleanup(curl);
}

// ============================================================
// 바이러스 검사
// ============================================================

bool DownloadManager::scanForVirus(DownloadItem& item) {
    // SecurityAgent와 통합 — 파일 해시 계산 후 검사
    // 현재는 기본적인 확장자 기반 위험도 판단 구현
    item.virus_scanned = true;
    item.virus_safe = true;

    // 위험한 실행 파일 확장자 경고
    auto ext = fs::path(item.save_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static const std::vector<std::string> dangerous_exts = {
        ".exe", ".msi", ".bat", ".cmd", ".com", ".scr", ".pif",
        ".vbs", ".vbe", ".js",  ".jse", ".wsf", ".wsh",
        ".ps1", ".psm1", ".psd1",
        ".app", ".action", ".command",
        ".sh",  ".csh", ".ksh",
        ".dll", ".sys", ".drv",
    };

    for (const auto& dext : dangerous_exts) {
        if (ext == dext) {
            item.virus_info = "위험한 파일 형식: " + ext +
                              " — 실행 전 주의가 필요합니다";
            // 실제로는 SecurityAgent의 분석 결과로 대체
            break;
        }
    }

    // 파일 크기가 0이면 의심
    std::error_code ec;
    auto fsize = fs::file_size(item.save_path, ec);
    if (!ec && fsize == 0) {
        item.virus_info = "빈 파일 (0바이트)";
    }

    // SHA-256 해시 계산 (SecurityAgent 연동용)
    // 실제 환경에서는 gRPC로 SecurityAgent에 해시를 전달하여 검사
    // 여기서는 해시 계산까지만 수행하고 결과는 안전으로 처리

    return item.virus_safe;
}

// ============================================================
// 내부 헬퍼
// ============================================================

void DownloadManager::persistState(const DownloadItem& item) {
    store_->execute(
        "UPDATE downloads SET filename = ?, save_path = ?, mime_type = ?, "
        "mime_category = ?, state = ?, total_bytes = ?, received_bytes = ?, "
        "error_message = ?, end_time = ?, virus_scanned = ?, virus_safe = ?, "
        "virus_info = ? WHERE id = ?",
        {
            item.filename, item.save_path, item.mime_type,
            static_cast<int64_t>(item.mime_category),
            static_cast<int64_t>(item.state),
            item.total_bytes, item.received_bytes, item.error_message,
            timeToString(item.end_time),
            static_cast<int64_t>(item.virus_scanned ? 1 : 0),
            static_cast<int64_t>(item.virus_safe ? 1 : 0),
            item.virus_info,
            item.id
        }
    );
}

void DownloadManager::updateState(int64_t id, DownloadState new_state) {
    {
        std::lock_guard lock(mutex_);
        auto it = active_downloads_.find(id);
        if (it != active_downloads_.end()) {
            it->second.state = new_state;
            persistState(it->second);
        }
    }
    if (state_callback_) {
        state_callback_(id, new_state);
    }
}

std::string DownloadManager::resolveFilename(const std::string& path) const {
    if (!fs::exists(path)) return path;

    auto parent = fs::path(path).parent_path();
    auto stem = fs::path(path).stem().string();
    auto ext = fs::path(path).extension().string();

    int counter = 1;
    std::string new_path;
    do {
        new_path = (parent / (stem + " (" + std::to_string(counter) + ")" + ext)).string();
        ++counter;
    } while (fs::exists(new_path) && counter < 10000);

    return new_path;
}

bool DownloadManager::isBlockedExtension(const std::string& filename) const {
    if (config_.blocked_extensions.empty()) return false;

    auto ext = fs::path(filename).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    for (const auto& blocked : config_.blocked_extensions) {
        std::string b = blocked;
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        if (!b.starts_with(".")) {
            b = "." + b;
        }
        if (ext == b) return true;
    }
    return false;
}

DownloadItem DownloadManager::rowToItem(
    const std::unordered_map<std::string,
        std::variant<std::nullptr_t, int64_t, double, std::string,
                     std::vector<uint8_t>>>& row) const {

    DownloadItem item;

    auto getInt = [&](const std::string& key) -> int64_t {
        auto it = row.find(key);
        if (it == row.end()) return 0;
        if (std::holds_alternative<int64_t>(it->second))
            return std::get<int64_t>(it->second);
        return 0;
    };

    auto getStr = [&](const std::string& key) -> std::string {
        auto it = row.find(key);
        if (it == row.end()) return "";
        if (std::holds_alternative<std::string>(it->second))
            return std::get<std::string>(it->second);
        return "";
    };

    item.id = getInt("id");
    item.url = getStr("url");
    item.filename = getStr("filename");
    item.save_path = getStr("save_path");
    item.mime_type = getStr("mime_type");
    item.mime_category = static_cast<MimeCategory>(getInt("mime_category"));
    item.state = static_cast<DownloadState>(getInt("state"));
    item.total_bytes = getInt("total_bytes");
    item.received_bytes = getInt("received_bytes");
    item.error_message = getStr("error_message");
    item.start_time = stringToTime(getStr("start_time"));
    item.end_time = stringToTime(getStr("end_time"));
    item.referrer_url = getStr("referrer_url");
    item.virus_scanned = getInt("virus_scanned") != 0;
    item.virus_safe = getInt("virus_safe") != 0;
    item.virus_info = getStr("virus_info");

    // 진행률 재계산
    if (item.total_bytes > 0) {
        item.progress_percent = static_cast<int>(
            (item.received_bytes * 100) / item.total_bytes);
    }

    return item;
}

} // namespace ordinal::data
