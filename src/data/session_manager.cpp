/**
 * @file session_manager.cpp
 * @brief 세션 관리자 구현
 *
 * 탭 상태를 JSON으로 직렬화하여 SQLite에 저장/복원,
 * 자동 저장 타이머, 크래시 복구, 명명된 세션, 시작 복원 설정.
 * 외부 JSON 라이브러리 없이 수동 직렬화/역직렬화 구현.
 */

#include "session_manager.h"
#include "data_store.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;
using Clock = std::chrono::system_clock;

namespace ordinal::data {

// ============================================================
// 내부 상수
// ============================================================

namespace {
constexpr const char* AUTO_SAVE_NAME = "__autosave__";
constexpr const char* CRASH_RECOVERY_NAME = "__crash_recovery__";
} // 익명 네임스페이스

// ============================================================
// 생성자 / 소멸자
// ============================================================

SessionManager::SessionManager(std::shared_ptr<DataStore> store)
    : store_(std::move(store)) {}

SessionManager::~SessionManager() {
    shutdown();
}

// ============================================================
// 초기화 / 종료
// ============================================================

bool SessionManager::initialize(const SessionConfig& config) {
    if (!store_ || !store_->isOpen()) return false;

    config_ = config;

    // 마이그레이션 — 세션 테이블
    Migration mig;
    mig.version = 400;
    mig.name = "세션 테이블 생성";
    mig.up_sql = R"SQL(
        CREATE TABLE IF NOT EXISTS sessions (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            name            TEXT NOT NULL DEFAULT '',
            json_data       TEXT NOT NULL DEFAULT '{}',
            is_auto_save    INTEGER DEFAULT 0,
            is_crash_recovery INTEGER DEFAULT 0,
            window_count    INTEGER DEFAULT 0,
            tab_count       INTEGER DEFAULT 0,
            created_at      TEXT NOT NULL,
            modified_at     TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_sessions_name ON sessions(name);
        CREATE INDEX IF NOT EXISTS idx_sessions_modified ON sessions(modified_at DESC);
        CREATE INDEX IF NOT EXISTS idx_sessions_auto ON sessions(is_auto_save);
        CREATE INDEX IF NOT EXISTS idx_sessions_crash ON sessions(is_crash_recovery);
    )SQL";
    mig.down_sql = "DROP TABLE IF EXISTS sessions;";

    store_->registerMigration(mig);
    store_->runMigrations();

    // 자동 저장 시작
    if (config_.auto_save_enabled) {
        running_ = true;
        auto_save_thread_ = std::thread(&SessionManager::autoSaveLoop, this);
    }

    return true;
}

void SessionManager::shutdown() {
    // 자동 저장 중지
    running_ = false;
    if (auto_save_thread_.joinable()) {
        auto_save_thread_.join();
    }

    // 마지막 세션 저장
    if (collect_callback_) {
        saveSession(AUTO_SAVE_NAME);
    }

    // 정상 종료 — 크래시 복구 세션 삭제
    clearCrashRecoverySession();
}

// ============================================================
// 세션 저장
// ============================================================

int64_t SessionManager::saveSession(const std::string& name) {
    std::string session_name = name.empty() ? AUTO_SAVE_NAME : name;
    bool is_auto = (session_name == AUTO_SAVE_NAME);

    // 현재 탭 상태 수집
    SessionData session;
    session.name = session_name;
    session.created_at = nowIso8601();
    session.modified_at = session.created_at;

    if (collect_callback_) {
        session.windows = collect_callback_();
    }

    if (session.windows.empty()) {
        // 탭이 없으면 빈 세션 저장하지 않음
        return -1;
    }

    std::string json = serializeToJson(session);

    return persistSession(session_name, json, is_auto, false);
}

int64_t SessionManager::saveSession(const SessionData& session) {
    std::string json = serializeToJson(session);
    bool is_auto = (session.name == AUTO_SAVE_NAME);
    return persistSession(session.name, json, is_auto, false);
}

bool SessionManager::saveCrashRecoverySession() {
    if (!collect_callback_) return false;

    SessionData session;
    session.name = CRASH_RECOVERY_NAME;
    session.created_at = nowIso8601();
    session.modified_at = session.created_at;
    session.windows = collect_callback_();

    if (session.windows.empty()) return false;

    std::string json = serializeToJson(session);

    // 기존 크래시 복구 세션 교체
    store_->execute(
        "DELETE FROM sessions WHERE is_crash_recovery = 1");

    int64_t id = persistSession(CRASH_RECOVERY_NAME, json, false, true);
    return id > 0;
}

// ============================================================
// 세션 복원
// ============================================================

bool SessionManager::restoreLastSession() {
    auto rows = store_->query(
        "SELECT json_data FROM sessions WHERE is_auto_save = 1 "
        "ORDER BY modified_at DESC LIMIT 1");

    if (rows.empty()) return false;

    auto it = rows.front().find("json_data");
    if (it == rows.front().end()) return false;

    std::string json;
    if (std::holds_alternative<std::string>(it->second)) {
        json = std::get<std::string>(it->second);
    } else {
        return false;
    }

    auto session = deserializeFromJson(json);
    if (!session) return false;

    if (!restore_callback_) return false;

    for (const auto& window : session->windows) {
        restore_callback_(window);
    }

    return true;
}

bool SessionManager::restoreSession(const std::string& name) {
    auto rows = store_->query(
        "SELECT json_data FROM sessions WHERE name = ? "
        "ORDER BY modified_at DESC LIMIT 1",
        {name});

    if (rows.empty()) return false;

    auto it = rows.front().find("json_data");
    if (it == rows.front().end()) return false;

    std::string json;
    if (std::holds_alternative<std::string>(it->second)) {
        json = std::get<std::string>(it->second);
    } else {
        return false;
    }

    auto session = deserializeFromJson(json);
    if (!session || !restore_callback_) return false;

    for (const auto& window : session->windows) {
        restore_callback_(window);
    }

    return true;
}

bool SessionManager::restoreSession(int64_t session_id) {
    auto rows = store_->query(
        "SELECT json_data FROM sessions WHERE id = ?",
        {session_id});

    if (rows.empty()) return false;

    auto it = rows.front().find("json_data");
    if (it == rows.front().end()) return false;

    std::string json;
    if (std::holds_alternative<std::string>(it->second)) {
        json = std::get<std::string>(it->second);
    } else {
        return false;
    }

    auto session = deserializeFromJson(json);
    if (!session || !restore_callback_) return false;

    for (const auto& window : session->windows) {
        restore_callback_(window);
    }

    return true;
}

bool SessionManager::hasCrashRecoverySession() const {
    auto result = store_->queryScalar(
        "SELECT COUNT(*) FROM sessions WHERE is_crash_recovery = 1");

    if (!result) return false;
    if (std::holds_alternative<int64_t>(*result)) {
        return std::get<int64_t>(*result) > 0;
    }
    return false;
}

bool SessionManager::restoreCrashRecoverySession() {
    auto rows = store_->query(
        "SELECT json_data FROM sessions WHERE is_crash_recovery = 1 "
        "ORDER BY modified_at DESC LIMIT 1");

    if (rows.empty()) return false;

    auto it = rows.front().find("json_data");
    if (it == rows.front().end()) return false;

    std::string json;
    if (std::holds_alternative<std::string>(it->second)) {
        json = std::get<std::string>(it->second);
    } else {
        return false;
    }

    auto session = deserializeFromJson(json);
    if (!session || !restore_callback_) return false;

    for (const auto& window : session->windows) {
        restore_callback_(window);
    }

    // 복원 후 크래시 세션 삭제
    clearCrashRecoverySession();
    return true;
}

int SessionManager::performStartupRestore() {
    int restored_tabs = 0;

    switch (config_.startup_action) {
        case StartupAction::NewTab:
            // 새 탭 — 복원하지 않음
            break;

        case StartupAction::RestoreLastSession:
            if (hasCrashRecoverySession()) {
                // 크래시 복구 세션이 있으면 우선 복원
                restoreCrashRecoverySession();
            } else {
                restoreLastSession();
            }
            break;

        case StartupAction::OpenHomePage:
            // 홈 페이지 — 복원 콜백으로 홈 페이지 탭 생성
            if (restore_callback_) {
                WindowState w;
                TabState t;
                t.current_url = config_.home_page_url;
                t.title = "홈";
                w.tabs.push_back(t);
                restore_callback_(w);
                restored_tabs = 1;
            }
            break;

        case StartupAction::OpenSpecificPages:
            if (restore_callback_ && !config_.startup_urls.empty()) {
                WindowState w;
                for (const auto& url : config_.startup_urls) {
                    TabState t;
                    t.current_url = url;
                    t.tab_index = static_cast<int>(w.tabs.size());
                    w.tabs.push_back(t);
                }
                restore_callback_(w);
                restored_tabs = static_cast<int>(w.tabs.size());
            }
            break;

        case StartupAction::RestoreNamedSession:
            if (!config_.named_session.empty()) {
                restoreSession(config_.named_session);
            }
            break;
    }

    return restored_tabs;
}

// ============================================================
// 세션 조회
// ============================================================

std::vector<SessionSummary> SessionManager::listSessions(bool include_auto) const {
    std::string sql =
        "SELECT id, name, created_at, modified_at, window_count, tab_count, "
        "is_auto_save, is_crash_recovery FROM sessions ";

    if (!include_auto) {
        sql += "WHERE is_auto_save = 0 AND is_crash_recovery = 0 ";
    }
    sql += "ORDER BY modified_at DESC";

    auto rows = store_->query(sql);
    std::vector<SessionSummary> result;
    result.reserve(rows.size());

    for (const auto& row : rows) {
        SessionSummary s;

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

        s.id = getInt("id");
        s.name = getStr("name");
        s.created_at = getStr("created_at");
        s.modified_at = getStr("modified_at");
        s.window_count = static_cast<int>(getInt("window_count"));
        s.tab_count = static_cast<int>(getInt("tab_count"));
        s.is_auto_save = getInt("is_auto_save") != 0;
        s.is_crash_recovery = getInt("is_crash_recovery") != 0;

        result.push_back(s);
    }

    return result;
}

std::optional<SessionData> SessionManager::getSession(int64_t session_id) const {
    auto rows = store_->query(
        "SELECT json_data FROM sessions WHERE id = ?",
        {session_id});

    if (rows.empty()) return std::nullopt;

    auto it = rows.front().find("json_data");
    if (it == rows.front().end()) return std::nullopt;

    if (!std::holds_alternative<std::string>(it->second)) return std::nullopt;

    return deserializeFromJson(std::get<std::string>(it->second));
}

std::optional<SessionData> SessionManager::getSession(const std::string& name) const {
    auto rows = store_->query(
        "SELECT json_data FROM sessions WHERE name = ? "
        "ORDER BY modified_at DESC LIMIT 1",
        {name});

    if (rows.empty()) return std::nullopt;

    auto it = rows.front().find("json_data");
    if (it == rows.front().end()) return std::nullopt;

    if (!std::holds_alternative<std::string>(it->second)) return std::nullopt;

    return deserializeFromJson(std::get<std::string>(it->second));
}

// ============================================================
// 세션 관리
// ============================================================

bool SessionManager::renameSession(int64_t session_id, const std::string& new_name) {
    return store_->execute(
        "UPDATE sessions SET name = ?, modified_at = ? WHERE id = ?",
        {new_name, nowIso8601(), session_id}
    ) > 0;
}

bool SessionManager::deleteSession(int64_t session_id) {
    return store_->execute(
        "DELETE FROM sessions WHERE id = ?",
        {session_id}
    ) > 0;
}

bool SessionManager::deleteSession(const std::string& name) {
    return store_->execute(
        "DELETE FROM sessions WHERE name = ?",
        {name}
    ) > 0;
}

int SessionManager::cleanupAutoSaves() {
    // 최신 max_saved_sessions개만 유지, 나머지 삭제
    auto count_result = store_->queryScalar(
        "SELECT COUNT(*) FROM sessions WHERE is_auto_save = 1");

    int64_t total = 0;
    if (count_result && std::holds_alternative<int64_t>(*count_result)) {
        total = std::get<int64_t>(*count_result);
    }

    if (total <= config_.max_saved_sessions) return 0;

    int64_t to_delete = total - config_.max_saved_sessions;

    // 가장 오래된 자동 저장 세션부터 삭제
    return store_->execute(
        "DELETE FROM sessions WHERE id IN ("
        "  SELECT id FROM sessions WHERE is_auto_save = 1 "
        "  ORDER BY modified_at ASC LIMIT ?"
        ")",
        {to_delete}
    );
}

void SessionManager::clearCrashRecoverySession() {
    store_->execute("DELETE FROM sessions WHERE is_crash_recovery = 1");
}

// ============================================================
// 콜백 등록
// ============================================================

void SessionManager::setRestoreCallback(SessionRestoreCallback callback) {
    restore_callback_ = std::move(callback);
}

void SessionManager::setCollectCallback(SessionCollectCallback callback) {
    collect_callback_ = std::move(callback);
}

// ============================================================
// 설정
// ============================================================

void SessionManager::updateConfig(const SessionConfig& config) {
    bool was_running = running_;

    // 자동 저장 재시작 필요 여부
    bool needs_restart = (config_.auto_save_enabled != config.auto_save_enabled ||
                          config_.auto_save_interval_sec != config.auto_save_interval_sec);

    config_ = config;

    if (needs_restart) {
        // 기존 스레드 종료
        running_ = false;
        if (auto_save_thread_.joinable()) {
            auto_save_thread_.join();
        }

        // 새 설정으로 재시작
        if (config_.auto_save_enabled) {
            running_ = true;
            auto_save_thread_ = std::thread(&SessionManager::autoSaveLoop, this);
        }
    }
}

// ============================================================
// JSON 직렬화 (수동 구현)
// ============================================================

std::string SessionManager::serializeToJson(const SessionData& session) {
    std::ostringstream os;
    os << "{\n";
    os << "  \"name\": \"" << jsonEscape(session.name) << "\",\n";
    os << "  \"created_at\": \"" << jsonEscape(session.created_at) << "\",\n";
    os << "  \"modified_at\": \"" << jsonEscape(session.modified_at) << "\",\n";

    // 메타데이터
    os << "  \"metadata\": {";
    bool first_meta = true;
    for (const auto& [key, val] : session.metadata) {
        if (!first_meta) os << ",";
        os << "\n    \"" << jsonEscape(key) << "\": \"" << jsonEscape(val) << "\"";
        first_meta = false;
    }
    os << "\n  },\n";

    // 창 목록
    os << "  \"windows\": [\n";
    for (size_t wi = 0; wi < session.windows.size(); ++wi) {
        const auto& w = session.windows[wi];
        os << "    {\n";
        os << "      \"window_id\": " << w.window_id << ",\n";
        os << "      \"x\": " << w.x << ", \"y\": " << w.y << ",\n";
        os << "      \"width\": " << w.width << ", \"height\": " << w.height << ",\n";
        os << "      \"maximized\": " << (w.maximized ? "true" : "false") << ",\n";
        os << "      \"fullscreen\": " << (w.fullscreen ? "true" : "false") << ",\n";
        os << "      \"active_tab_index\": " << w.active_tab_index << ",\n";

        // 탭 목록
        os << "      \"tabs\": [\n";
        for (size_t ti = 0; ti < w.tabs.size(); ++ti) {
            const auto& t = w.tabs[ti];
            os << "        {\n";
            os << "          \"tab_index\": " << t.tab_index << ",\n";
            os << "          \"current_url\": \"" << jsonEscape(t.current_url) << "\",\n";
            os << "          \"title\": \"" << jsonEscape(t.title) << "\",\n";
            os << "          \"favicon_url\": \"" << jsonEscape(t.favicon_url) << "\",\n";
            os << "          \"pinned\": " << (t.pinned ? "true" : "false") << ",\n";
            os << "          \"muted\": " << (t.muted ? "true" : "false") << ",\n";
            os << "          \"scroll_x\": " << t.scroll_x << ",\n";
            os << "          \"scroll_y\": " << t.scroll_y << ",\n";
            os << "          \"zoom_factor\": " << t.zoom_factor << ",\n";
            os << "          \"nav_index\": " << t.nav_index << ",\n";

            // 네비게이션 히스토리
            os << "          \"nav_history\": [\n";
            for (size_t ni = 0; ni < t.nav_history.size(); ++ni) {
                const auto& nav = t.nav_history[ni];
                os << "            {\"url\": \"" << jsonEscape(nav.url)
                   << "\", \"title\": \"" << jsonEscape(nav.title)
                   << "\", \"timestamp\": " << nav.timestamp << "}";
                if (ni + 1 < t.nav_history.size()) os << ",";
                os << "\n";
            }
            os << "          ],\n";

            // 폼 데이터
            os << "          \"form_data\": {";
            bool first_form = true;
            for (const auto& [key, val] : t.form_data) {
                if (!first_form) os << ",";
                os << "\n            \"" << jsonEscape(key) << "\": \""
                   << jsonEscape(val) << "\"";
                first_form = false;
            }
            os << "\n          }\n";

            os << "        }";
            if (ti + 1 < w.tabs.size()) os << ",";
            os << "\n";
        }
        os << "      ]\n";

        os << "    }";
        if (wi + 1 < session.windows.size()) os << ",";
        os << "\n";
    }
    os << "  ]\n";
    os << "}";

    return os.str();
}

std::optional<SessionData> SessionManager::deserializeFromJson(const std::string& json) {
    // 간이 JSON 파서 — SessionData 구조에 특화
    // 실제 프로덕션에서는 nlohmann/json 또는 RapidJSON을 사용하지만,
    // 외부 의존성 없이 자체 구현

    SessionData session;

    // 유틸리티 람다: 키 뒤의 문자열 값 추출
    auto extractString = [&](const std::string& src, const std::string& key,
                             size_t start = 0) -> std::pair<std::string, size_t> {
        std::string search = "\"" + key + "\"";
        size_t pos = src.find(search, start);
        if (pos == std::string::npos) return {"", std::string::npos};

        pos = src.find(':', pos + search.size());
        if (pos == std::string::npos) return {"", std::string::npos};

        // 문자열 값 찾기
        size_t q1 = src.find('"', pos + 1);
        if (q1 == std::string::npos) return {"", std::string::npos};

        // 닫는 따옴표 (이스케이프 고려)
        size_t q2 = q1 + 1;
        while (q2 < src.size()) {
            if (src[q2] == '"' && src[q2 - 1] != '\\') break;
            ++q2;
        }
        return {jsonUnescape(src.substr(q1 + 1, q2 - q1 - 1)), q2 + 1};
    };

    // 유틸리티: 키 뒤의 숫자 값 추출
    auto extractNumber = [&](const std::string& src, const std::string& key,
                             size_t start = 0) -> std::pair<int64_t, size_t> {
        std::string search = "\"" + key + "\"";
        size_t pos = src.find(search, start);
        if (pos == std::string::npos) return {0, std::string::npos};

        pos = src.find(':', pos + search.size());
        if (pos == std::string::npos) return {0, std::string::npos};

        ++pos;
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t')) ++pos;

        size_t end = pos;
        bool has_dot = false;
        if (end < src.size() && src[end] == '-') ++end;
        while (end < src.size() && (std::isdigit(src[end]) || src[end] == '.')) {
            if (src[end] == '.') has_dot = true;
            ++end;
        }

        std::string num_str = src.substr(pos, end - pos);
        try {
            return {std::stoll(num_str), end};
        } catch (...) {
            return {0, end};
        }
    };

    // 유틸리티: 키 뒤의 실수 값 추출
    auto extractDouble = [&](const std::string& src, const std::string& key,
                             size_t start = 0) -> std::pair<double, size_t> {
        std::string search = "\"" + key + "\"";
        size_t pos = src.find(search, start);
        if (pos == std::string::npos) return {0.0, std::string::npos};

        pos = src.find(':', pos + search.size());
        if (pos == std::string::npos) return {0.0, std::string::npos};

        ++pos;
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t')) ++pos;

        size_t end = pos;
        if (end < src.size() && src[end] == '-') ++end;
        while (end < src.size() && (std::isdigit(src[end]) || src[end] == '.')) ++end;

        try {
            return {std::stod(src.substr(pos, end - pos)), end};
        } catch (...) {
            return {0.0, end};
        }
    };

    // 유틸리티: 키 뒤의 불리언 값 추출
    auto extractBool = [&](const std::string& src, const std::string& key,
                           size_t start = 0) -> std::pair<bool, size_t> {
        std::string search = "\"" + key + "\"";
        size_t pos = src.find(search, start);
        if (pos == std::string::npos) return {false, std::string::npos};

        pos = src.find(':', pos + search.size());
        if (pos == std::string::npos) return {false, std::string::npos};

        ++pos;
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t')) ++pos;

        if (src.substr(pos, 4) == "true") return {true, pos + 4};
        return {false, pos + 5};
    };

    // 최상위 필드 추출
    auto [name, _1] = extractString(json, "name");
    session.name = name;
    auto [created, _2] = extractString(json, "created_at");
    session.created_at = created;
    auto [modified, _3] = extractString(json, "modified_at");
    session.modified_at = modified;

    // 창 배열 파싱 — "windows": [ 위치 찾기
    size_t windows_pos = json.find("\"windows\"");
    if (windows_pos == std::string::npos) return session;

    size_t arr_start = json.find('[', windows_pos);
    if (arr_start == std::string::npos) return session;

    // 각 창 객체 { ... } 파싱
    size_t pos = arr_start + 1;
    while (pos < json.size()) {
        // 다음 창 객체 시작
        size_t obj_start = json.find('{', pos);
        if (obj_start == std::string::npos) break;

        // 이 객체가 windows 배열의 끝(])을 넘어가면 중단
        size_t arr_end_check = json.find(']', pos);
        if (arr_end_check != std::string::npos && arr_end_check < obj_start) break;

        // 대응하는 닫는 괄호 찾기 (중첩 고려)
        int depth = 0;
        size_t obj_end = obj_start;
        for (; obj_end < json.size(); ++obj_end) {
            if (json[obj_end] == '{') ++depth;
            else if (json[obj_end] == '}') {
                --depth;
                if (depth == 0) break;
            }
        }

        std::string win_json = json.substr(obj_start, obj_end - obj_start + 1);

        WindowState w;
        w.window_id = static_cast<int>(extractNumber(win_json, "window_id").first);
        w.x = static_cast<int>(extractNumber(win_json, "x").first);
        w.y = static_cast<int>(extractNumber(win_json, "y").first);
        w.width = static_cast<int>(extractNumber(win_json, "width").first);
        w.height = static_cast<int>(extractNumber(win_json, "height").first);
        w.maximized = extractBool(win_json, "maximized").first;
        w.fullscreen = extractBool(win_json, "fullscreen").first;
        w.active_tab_index = static_cast<int>(
            extractNumber(win_json, "active_tab_index").first);

        // 탭 배열 파싱
        size_t tabs_pos = win_json.find("\"tabs\"");
        if (tabs_pos != std::string::npos) {
            size_t tab_arr = win_json.find('[', tabs_pos);
            if (tab_arr != std::string::npos) {
                size_t tpos = tab_arr + 1;
                while (tpos < win_json.size()) {
                    size_t tab_start = win_json.find('{', tpos);
                    if (tab_start == std::string::npos) break;

                    // tabs 배열 종료 확인
                    size_t tab_arr_end = win_json.find(']', tpos);
                    if (tab_arr_end != std::string::npos && tab_arr_end < tab_start) break;

                    // 탭 객체 끝 찾기 (nav_history 중첩 고려)
                    int tdepth = 0;
                    size_t tab_end = tab_start;
                    for (; tab_end < win_json.size(); ++tab_end) {
                        if (win_json[tab_end] == '{') ++tdepth;
                        else if (win_json[tab_end] == '}') {
                            --tdepth;
                            if (tdepth == 0) break;
                        }
                    }

                    std::string tab_json = win_json.substr(
                        tab_start, tab_end - tab_start + 1);

                    TabState t;
                    t.tab_index = static_cast<int>(
                        extractNumber(tab_json, "tab_index").first);
                    t.current_url = extractString(tab_json, "current_url").first;
                    t.title = extractString(tab_json, "title").first;
                    t.favicon_url = extractString(tab_json, "favicon_url").first;
                    t.pinned = extractBool(tab_json, "pinned").first;
                    t.muted = extractBool(tab_json, "muted").first;
                    t.scroll_x = static_cast<int>(
                        extractNumber(tab_json, "scroll_x").first);
                    t.scroll_y = static_cast<int>(
                        extractNumber(tab_json, "scroll_y").first);
                    t.zoom_factor = extractDouble(tab_json, "zoom_factor").first;
                    if (t.zoom_factor == 0.0) t.zoom_factor = 1.0;
                    t.nav_index = static_cast<int>(
                        extractNumber(tab_json, "nav_index").first);

                    // nav_history 파싱
                    size_t nh_pos = tab_json.find("\"nav_history\"");
                    if (nh_pos != std::string::npos) {
                        size_t nh_arr = tab_json.find('[', nh_pos);
                        if (nh_arr != std::string::npos) {
                            size_t npos = nh_arr + 1;
                            while (npos < tab_json.size()) {
                                size_t nh_start = tab_json.find('{', npos);
                                if (nh_start == std::string::npos) break;

                                size_t nh_arr_end = tab_json.find(']', npos);
                                if (nh_arr_end != std::string::npos &&
                                    nh_arr_end < nh_start) break;

                                size_t nh_end = tab_json.find('}', nh_start);
                                if (nh_end == std::string::npos) break;

                                std::string nh_json = tab_json.substr(
                                    nh_start, nh_end - nh_start + 1);

                                NavHistoryEntry entry;
                                entry.url = extractString(nh_json, "url").first;
                                entry.title = extractString(nh_json, "title").first;
                                entry.timestamp = extractNumber(
                                    nh_json, "timestamp").first;

                                t.nav_history.push_back(entry);
                                npos = nh_end + 1;
                            }
                        }
                    }

                    w.tabs.push_back(t);
                    tpos = tab_end + 1;
                }
            }
        }

        session.windows.push_back(w);
        pos = obj_end + 1;
    }

    return session;
}

// ============================================================
// 내보내기 / 가져오기
// ============================================================

bool SessionManager::exportSession(int64_t session_id,
                                    const std::string& file_path) const {
    auto session = getSession(session_id);
    if (!session) return false;

    std::string json = serializeToJson(*session);

    std::ofstream ofs(file_path);
    if (!ofs.is_open()) return false;
    ofs << json;
    return true;
}

int64_t SessionManager::importSession(const std::string& file_path) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) return -1;

    std::string json((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());

    auto session = deserializeFromJson(json);
    if (!session) return -1;

    // 가져온 세션의 이름에 "(imported)" 추가
    if (session->name.empty() || session->name == AUTO_SAVE_NAME) {
        session->name = fs::path(file_path).stem().string();
    }
    session->name += " (가져옴)";
    session->modified_at = nowIso8601();

    return saveSession(*session);
}

// ============================================================
// 내부 헬퍼
// ============================================================

void SessionManager::autoSaveLoop() {
    while (running_) {
        // 지정된 간격만큼 대기
        for (int i = 0; i < config_.auto_save_interval_sec && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!running_) break;

        // 자동 저장 수행
        if (collect_callback_) {
            saveSession(AUTO_SAVE_NAME);

            // 크래시 복구 세션도 업데이트
            if (config_.crash_recovery_enabled) {
                saveCrashRecoverySession();
            }

            // 오래된 자동 저장 정리
            cleanupAutoSaves();
        }
    }
}

int64_t SessionManager::persistSession(const std::string& name,
                                        const std::string& json,
                                        bool is_auto, bool is_crash) {
    std::string now = nowIso8601();

    // 탭/창 수 계산 (JSON에서)
    int window_count = 0;
    int tab_count = 0;
    auto session = deserializeFromJson(json);
    if (session) {
        window_count = static_cast<int>(session->windows.size());
        for (const auto& w : session->windows) {
            tab_count += static_cast<int>(w.tabs.size());
        }
    }

    // 동일 이름의 자동 저장 세션이 있으면 업데이트
    if (is_auto || is_crash) {
        auto existing = store_->query(
            "SELECT id FROM sessions WHERE name = ? AND "
            "(is_auto_save = ? OR is_crash_recovery = ?) "
            "ORDER BY modified_at DESC LIMIT 1",
            {name, static_cast<int64_t>(is_auto ? 1 : 0),
             static_cast<int64_t>(is_crash ? 1 : 0)});

        if (!existing.empty()) {
            auto it = existing.front().find("id");
            if (it != existing.front().end() &&
                std::holds_alternative<int64_t>(it->second)) {
                int64_t existing_id = std::get<int64_t>(it->second);
                store_->execute(
                    "UPDATE sessions SET json_data = ?, modified_at = ?, "
                    "window_count = ?, tab_count = ? WHERE id = ?",
                    {json, now, static_cast<int64_t>(window_count),
                     static_cast<int64_t>(tab_count), existing_id});
                return existing_id;
            }
        }
    }

    // 새 레코드 삽입
    store_->execute(
        "INSERT INTO sessions (name, json_data, is_auto_save, is_crash_recovery, "
        "window_count, tab_count, created_at, modified_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        {name, json,
         static_cast<int64_t>(is_auto ? 1 : 0),
         static_cast<int64_t>(is_crash ? 1 : 0),
         static_cast<int64_t>(window_count),
         static_cast<int64_t>(tab_count),
         now, now});

    return store_->lastInsertRowId();
}

std::string SessionManager::nowIso8601() {
    auto now = Clock::now();
    auto t = Clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string SessionManager::jsonEscape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            case '\b': result += "\\b";  break;
            case '\f': result += "\\f";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char hex[8];
                    std::snprintf(hex, sizeof(hex), "\\u%04x",
                                  static_cast<unsigned int>(c));
                    result += hex;
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

std::string SessionManager::jsonUnescape(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '"':  result += '"';  ++i; break;
                case '\\': result += '\\'; ++i; break;
                case 'n':  result += '\n'; ++i; break;
                case 'r':  result += '\r'; ++i; break;
                case 't':  result += '\t'; ++i; break;
                case 'b':  result += '\b'; ++i; break;
                case 'f':  result += '\f'; ++i; break;
                case 'u':
                    // \uXXXX — 4자리 16진수
                    if (i + 5 < s.size()) {
                        try {
                            int code = std::stoi(s.substr(i + 2, 4), nullptr, 16);
                            if (code < 0x80) {
                                result += static_cast<char>(code);
                            } else if (code < 0x800) {
                                result += static_cast<char>(0xC0 | (code >> 6));
                                result += static_cast<char>(0x80 | (code & 0x3F));
                            } else {
                                result += static_cast<char>(0xE0 | (code >> 12));
                                result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                                result += static_cast<char>(0x80 | (code & 0x3F));
                            }
                            i += 5;
                        } catch (...) {
                            result += s[i];
                        }
                    }
                    break;
                default:
                    result += s[i];
                    break;
            }
        } else {
            result += s[i];
        }
    }
    return result;
}

} // namespace ordinal::data
