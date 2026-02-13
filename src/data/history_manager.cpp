/**
 * @file history_manager.cpp
 * @brief 방문 기록 관리자 구현
 * 
 * 방문 기록 저장(URL/제목/타임스탬프/체류시간), 텍스트 + 날짜 범위 검색,
 * 자주 방문한 사이트, 최근 닫은 탭, 시간대별 삭제 전체 구현.
 */

#include "history_manager.h"
#include "data_store.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ordinal::data {

// ============================================================
// 생성자 / 소멸자
// ============================================================

HistoryManager::HistoryManager(std::shared_ptr<DataStore> store)
    : store_(std::move(store)) {}

HistoryManager::~HistoryManager() = default;

// ============================================================
// 초기화
// ============================================================

bool HistoryManager::initialize() {
    if (!store_ || !store_->isOpen()) return false;

    Migration mig;
    mig.version = 200; // 히스토리 시작 버전
    mig.name = "방문 기록 테이블 생성";
    mig.up_sql = R"SQL(
        CREATE TABLE IF NOT EXISTS history (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            url             TEXT NOT NULL,
            title           TEXT NOT NULL DEFAULT '',
            visit_time      TEXT NOT NULL DEFAULT (datetime('now')),
            duration_sec    INTEGER NOT NULL DEFAULT 0,
            visit_count     INTEGER NOT NULL DEFAULT 1,
            favicon_url     TEXT DEFAULT '',
            referrer_url    TEXT DEFAULT '',
            is_typed        INTEGER NOT NULL DEFAULT 0
        );

        CREATE INDEX IF NOT EXISTS idx_history_url ON history(url);
        CREATE INDEX IF NOT EXISTS idx_history_time ON history(visit_time);
        CREATE INDEX IF NOT EXISTS idx_history_title ON history(title);

        CREATE TABLE IF NOT EXISTS closed_tabs (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            url         TEXT NOT NULL,
            title       TEXT NOT NULL DEFAULT '',
            closed_at   TEXT NOT NULL DEFAULT (datetime('now')),
            tab_index   INTEGER NOT NULL DEFAULT 0
        );

        CREATE INDEX IF NOT EXISTS idx_closed_tabs_time ON closed_tabs(closed_at);
    )SQL";
    mig.down_sql = "DROP TABLE IF EXISTS closed_tabs; DROP TABLE IF EXISTS history;";

    store_->registerMigration(mig);
    return store_->runMigrations() >= 0;
}

// ============================================================
// 방문 기록
// ============================================================

int64_t HistoryManager::recordVisit(const std::string& url, const std::string& title,
                                     const std::string& referrer, bool is_typed) {
    std::lock_guard lock(mutex_);

    // 같은 URL의 기존 기록이 있으면 visit_count 증가
    auto existing = store_->query(
        "SELECT id, visit_count FROM history WHERE url = ? ORDER BY visit_time DESC LIMIT 1",
        {url}
    );

    int visit_count = 1;
    if (!existing.empty()) {
        auto it = existing.front().find("visit_count");
        if (it != existing.front().end() && std::holds_alternative<int64_t>(it->second)) {
            visit_count = static_cast<int>(std::get<int64_t>(it->second)) + 1;
        }
    }

    store_->execute(
        "INSERT INTO history (url, title, visit_time, visit_count, referrer_url, is_typed) "
        "VALUES (?, ?, datetime('now'), ?, ?, ?)",
        {url, title, static_cast<int64_t>(visit_count), referrer,
         static_cast<int64_t>(is_typed ? 1 : 0)}
    );

    return store_->lastInsertRowId();
}

bool HistoryManager::updateDuration(int64_t record_id, int duration_seconds) {
    std::lock_guard lock(mutex_);
    return store_->execute(
        "UPDATE history SET duration_sec = ? WHERE id = ?",
        {static_cast<int64_t>(duration_seconds), record_id}
    ) >= 0;
}

bool HistoryManager::updateTitle(int64_t record_id, const std::string& title) {
    std::lock_guard lock(mutex_);
    return store_->execute(
        "UPDATE history SET title = ? WHERE id = ?",
        {title, record_id}
    ) >= 0;
}

// ============================================================
// 검색
// ============================================================

std::vector<VisitRecord> HistoryManager::search(const std::string& query, int max_results) const {
    std::lock_guard lock(mutex_);

    std::string pattern = "%" + query + "%";
    auto rows = store_->query(
        "SELECT * FROM history WHERE title LIKE ? OR url LIKE ? "
        "ORDER BY visit_time DESC LIMIT ?",
        {pattern, pattern, static_cast<int64_t>(max_results)}
    );

    std::vector<VisitRecord> results;
    results.reserve(rows.size());
    for (const auto& row : rows) {
        results.push_back(rowToRecord(row));
    }
    return results;
}

std::vector<VisitRecord> HistoryManager::searchWithFilter(const HistorySearchFilter& filter) const {
    std::lock_guard lock(mutex_);

    std::string sql = "SELECT * FROM history WHERE 1=1";
    std::vector<DbValue> params;

    // 텍스트 검색
    if (!filter.text_query.empty()) {
        std::string pattern = "%" + filter.text_query + "%";
        sql += " AND (title LIKE ? OR url LIKE ?)";
        params.push_back(pattern);
        params.push_back(pattern);
    }

    // 날짜 범위
    if (filter.from.has_value()) {
        sql += " AND visit_time >= ?";
        params.push_back(timeToString(*filter.from));
    }
    if (filter.to.has_value()) {
        sql += " AND visit_time <= ?";
        params.push_back(timeToString(*filter.to));
    }

    // 주소창 입력만
    if (filter.typed_only) {
        sql += " AND is_typed = 1";
    }

    sql += " ORDER BY visit_time DESC";

    // 페이징
    sql += " LIMIT ? OFFSET ?";
    params.push_back(static_cast<int64_t>(filter.max_results));
    params.push_back(static_cast<int64_t>(filter.offset));

    auto rows = store_->query(sql, params);

    std::vector<VisitRecord> results;
    results.reserve(rows.size());
    for (const auto& row : rows) {
        results.push_back(rowToRecord(row));
    }
    return results;
}

std::vector<VisitRecord> HistoryManager::getByDateRange(
    std::chrono::system_clock::time_point from,
    std::chrono::system_clock::time_point to) const {
    std::lock_guard lock(mutex_);

    auto rows = store_->query(
        "SELECT * FROM history WHERE visit_time BETWEEN ? AND ? ORDER BY visit_time DESC",
        {timeToString(from), timeToString(to)}
    );

    std::vector<VisitRecord> results;
    for (const auto& row : rows) {
        results.push_back(rowToRecord(row));
    }
    return results;
}

std::vector<VisitRecord> HistoryManager::getRecent(int count) const {
    std::lock_guard lock(mutex_);

    auto rows = store_->query(
        "SELECT * FROM history ORDER BY visit_time DESC LIMIT ?",
        {static_cast<int64_t>(count)}
    );

    std::vector<VisitRecord> results;
    for (const auto& row : rows) {
        results.push_back(rowToRecord(row));
    }
    return results;
}

bool HistoryManager::hasVisited(const std::string& url) const {
    auto val = store_->queryScalar(
        "SELECT COUNT(*) FROM history WHERE url = ?", {url});
    if (val && std::holds_alternative<int64_t>(*val)) {
        return std::get<int64_t>(*val) > 0;
    }
    return false;
}

int HistoryManager::getVisitCount(const std::string& url) const {
    auto val = store_->queryScalar(
        "SELECT COUNT(*) FROM history WHERE url = ?", {url});
    if (val && std::holds_alternative<int64_t>(*val)) {
        return static_cast<int>(std::get<int64_t>(*val));
    }
    return 0;
}

// ============================================================
// 자주 방문한 사이트
// ============================================================

std::vector<FrequentSite> HistoryManager::getMostVisited(int count) const {
    std::lock_guard lock(mutex_);

    auto rows = store_->query(
        "SELECT url, title, favicon_url, "
        "  COUNT(*) as total_visits, "
        "  SUM(duration_sec) as total_duration, "
        "  MAX(visit_time) as last_visit "
        "FROM history "
        "GROUP BY url "
        "ORDER BY total_visits DESC "
        "LIMIT ?",
        {static_cast<int64_t>(count)}
    );

    std::vector<FrequentSite> results;
    for (const auto& row : rows) {
        FrequentSite site;

        auto getStr = [&](const std::string& key) -> std::string {
            auto it = row.find(key);
            if (it != row.end() && std::holds_alternative<std::string>(it->second))
                return std::get<std::string>(it->second);
            return "";
        };

        auto getInt = [&](const std::string& key) -> int64_t {
            auto it = row.find(key);
            if (it != row.end() && std::holds_alternative<int64_t>(it->second))
                return std::get<int64_t>(it->second);
            return 0;
        };

        site.url = getStr("url");
        site.title = getStr("title");
        site.favicon_url = getStr("favicon_url");
        site.total_visits = static_cast<int>(getInt("total_visits"));
        site.total_duration_seconds = static_cast<int>(getInt("total_duration"));

        // last_visit 파싱
        std::string last_str = getStr("last_visit");
        if (!last_str.empty()) {
            std::tm tm = {};
            std::istringstream ss(last_str);
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            if (!ss.fail()) {
                site.last_visit = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            }
        }

        results.push_back(std::move(site));
    }

    return results;
}

// ============================================================
// 최근 닫은 탭
// ============================================================

void HistoryManager::recordClosedTab(const std::string& url, const std::string& title,
                                      int tab_index) {
    std::lock_guard lock(mutex_);

    store_->execute(
        "INSERT INTO closed_tabs (url, title, closed_at, tab_index) "
        "VALUES (?, ?, datetime('now'), ?)",
        {url, title, static_cast<int64_t>(tab_index)}
    );

    // 최대 100개까지만 유지
    store_->execute(
        "DELETE FROM closed_tabs WHERE id NOT IN "
        "(SELECT id FROM closed_tabs ORDER BY closed_at DESC LIMIT 100)"
    );
}

std::vector<ClosedTab> HistoryManager::getRecentlyClosed(int count) const {
    std::lock_guard lock(mutex_);

    auto rows = store_->query(
        "SELECT * FROM closed_tabs ORDER BY closed_at DESC LIMIT ?",
        {static_cast<int64_t>(count)}
    );

    std::vector<ClosedTab> results;
    for (const auto& row : rows) {
        ClosedTab tab;

        auto getStr = [&](const std::string& key) -> std::string {
            auto it = row.find(key);
            if (it != row.end() && std::holds_alternative<std::string>(it->second))
                return std::get<std::string>(it->second);
            return "";
        };

        auto getInt = [&](const std::string& key) -> int64_t {
            auto it = row.find(key);
            if (it != row.end() && std::holds_alternative<int64_t>(it->second))
                return std::get<int64_t>(it->second);
            return 0;
        };

        tab.id = getInt("id");
        tab.url = getStr("url");
        tab.title = getStr("title");
        tab.tab_index = static_cast<int>(getInt("tab_index"));

        std::string closed_str = getStr("closed_at");
        if (!closed_str.empty()) {
            std::tm tm = {};
            std::istringstream ss(closed_str);
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            if (!ss.fail()) {
                tab.closed_at = std::chrono::system_clock::from_time_t(std::mktime(&tm));
            }
        }

        results.push_back(std::move(tab));
    }

    return results;
}

void HistoryManager::clearClosedTabs() {
    std::lock_guard lock(mutex_);
    store_->execute("DELETE FROM closed_tabs");
}

// ============================================================
// 삭제
// ============================================================

int HistoryManager::clearByTimeRange(ClearTimeRange range) {
    std::lock_guard lock(mutex_);

    if (range == ClearTimeRange::AllTime) {
        return store_->execute("DELETE FROM history");
    }

    std::string where = timeRangeToSql(range);
    return store_->execute("DELETE FROM history WHERE " + where);
}

bool HistoryManager::removeByUrl(const std::string& url) {
    std::lock_guard lock(mutex_);
    return store_->execute("DELETE FROM history WHERE url = ?", {url}) >= 0;
}

bool HistoryManager::removeById(int64_t id) {
    std::lock_guard lock(mutex_);
    return store_->execute("DELETE FROM history WHERE id = ?", {id}) >= 0;
}

bool HistoryManager::clearAll() {
    std::lock_guard lock(mutex_);
    store_->execute("DELETE FROM closed_tabs");
    return store_->execute("DELETE FROM history") >= 0;
}

// ============================================================
// 통계
// ============================================================

int64_t HistoryManager::totalRecords() const {
    auto val = store_->queryScalar("SELECT COUNT(*) FROM history");
    if (val && std::holds_alternative<int64_t>(*val)) {
        return std::get<int64_t>(*val);
    }
    return 0;
}

int HistoryManager::todayUniqueVisits() const {
    auto val = store_->queryScalar(
        "SELECT COUNT(DISTINCT url) FROM history WHERE date(visit_time) = date('now')"
    );
    if (val && std::holds_alternative<int64_t>(*val)) {
        return static_cast<int>(std::get<int64_t>(*val));
    }
    return 0;
}

// ============================================================
// 내부 헬퍼
// ============================================================

VisitRecord HistoryManager::rowToRecord(
    const std::unordered_map<std::string,
        std::variant<std::nullptr_t, int64_t, double, std::string, std::vector<uint8_t>>>& row) const {

    VisitRecord rec;

    auto getStr = [&](const std::string& key) -> std::string {
        auto it = row.find(key);
        if (it != row.end() && std::holds_alternative<std::string>(it->second))
            return std::get<std::string>(it->second);
        return "";
    };

    auto getInt = [&](const std::string& key) -> int64_t {
        auto it = row.find(key);
        if (it != row.end() && std::holds_alternative<int64_t>(it->second))
            return std::get<int64_t>(it->second);
        return 0;
    };

    rec.id = getInt("id");
    rec.url = getStr("url");
    rec.title = getStr("title");
    rec.duration_seconds = static_cast<int>(getInt("duration_sec"));
    rec.visit_count = static_cast<int>(getInt("visit_count"));
    rec.favicon_url = getStr("favicon_url");
    rec.referrer_url = getStr("referrer_url");
    rec.is_typed = getInt("is_typed") != 0;

    // visit_time 파싱
    std::string time_str = getStr("visit_time");
    if (!time_str.empty()) {
        std::tm tm = {};
        std::istringstream ss(time_str);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (!ss.fail()) {
            rec.visit_time = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
    }

    return rec;
}

std::string HistoryManager::timeRangeToSql(ClearTimeRange range) const {
    switch (range) {
        case ClearTimeRange::LastHour:
            return "visit_time >= datetime('now', '-1 hour')";
        case ClearTimeRange::Last24Hours:
            return "visit_time >= datetime('now', '-1 day')";
        case ClearTimeRange::Last7Days:
            return "visit_time >= datetime('now', '-7 days')";
        case ClearTimeRange::Last30Days:
            return "visit_time >= datetime('now', '-30 days')";
        case ClearTimeRange::AllTime:
            return "1=1";
    }
    return "1=1";
}

std::string HistoryManager::nowString() {
    auto now = std::chrono::system_clock::now();
    return timeToString(now);
}

std::string HistoryManager::timeToString(std::chrono::system_clock::time_point tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
    gmtime_r(&time_t, &tm);

    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

} // namespace ordinal::data
