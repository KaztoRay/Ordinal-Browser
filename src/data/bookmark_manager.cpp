/**
 * @file bookmark_manager.cpp
 * @brief 북마크 관리자 구현
 * 
 * SQLite 기반 트리 구조 북마크 CRUD, 태그, 검색, 정렬,
 * HTML/Chrome JSON 가져오기·내보내기 전체 구현.
 */

#include "bookmark_manager.h"
#include "data_store.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>
#include <stack>

namespace ordinal::data {

// ============================================================
// 생성자 / 소멸자
// ============================================================

BookmarkManager::BookmarkManager(std::shared_ptr<DataStore> store)
    : store_(std::move(store)) {}

BookmarkManager::~BookmarkManager() = default;

// ============================================================
// 초기화
// ============================================================

bool BookmarkManager::initialize() {
    if (!store_ || !store_->isOpen()) return false;

    // 마이그레이션 등록 — 북마크 테이블
    Migration mig;
    mig.version = 100; // 북마크 관련 시작 버전
    mig.name = "북마크 테이블 생성";
    mig.up_sql = R"SQL(
        CREATE TABLE IF NOT EXISTS bookmarks (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            parent_id       INTEGER NOT NULL DEFAULT 0,
            type            INTEGER NOT NULL DEFAULT 1,  -- 0=폴더, 1=북마크, 2=구분선
            title           TEXT NOT NULL DEFAULT '',
            url             TEXT DEFAULT '',
            description     TEXT DEFAULT '',
            favicon_url     TEXT DEFAULT '',
            position        INTEGER NOT NULL DEFAULT 0,
            visit_count     INTEGER NOT NULL DEFAULT 0,
            date_added      TEXT NOT NULL DEFAULT (datetime('now')),
            date_modified   TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (parent_id) REFERENCES bookmarks(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_bookmarks_parent ON bookmarks(parent_id);
        CREATE INDEX IF NOT EXISTS idx_bookmarks_url ON bookmarks(url);

        CREATE TABLE IF NOT EXISTS bookmark_tags (
            bookmark_id     INTEGER NOT NULL,
            tag             TEXT NOT NULL,
            PRIMARY KEY (bookmark_id, tag),
            FOREIGN KEY (bookmark_id) REFERENCES bookmarks(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_bookmark_tags_tag ON bookmark_tags(tag);
    )SQL";
    mig.down_sql = "DROP TABLE IF EXISTS bookmark_tags; DROP TABLE IF EXISTS bookmarks;";

    store_->registerMigration(mig);
    int result = store_->runMigrations();
    if (result < 0) return false;

    createDefaultFolders();
    return true;
}

void BookmarkManager::createDefaultFolders() {
    // 기본 폴더가 이미 있는지 확인
    auto rows = store_->query(
        "SELECT id, title FROM bookmarks WHERE parent_id = 0 AND type = 0 ORDER BY position");

    std::unordered_map<std::string, int64_t> existing;
    for (const auto& row : rows) {
        auto title_it = row.find("title");
        auto id_it = row.find("id");
        if (title_it != row.end() && id_it != row.end()) {
            if (std::holds_alternative<std::string>(title_it->second) &&
                std::holds_alternative<int64_t>(id_it->second)) {
                existing[std::get<std::string>(title_it->second)] =
                    std::get<int64_t>(id_it->second);
            }
        }
    }

    // 북마크 바
    if (existing.count("북마크 바") == 0) {
        store_->execute(
            "INSERT INTO bookmarks (parent_id, type, title, position) VALUES (0, 0, '북마크 바', 0)");
        toolbar_folder_id_ = store_->lastInsertRowId();
    } else {
        toolbar_folder_id_ = existing["북마크 바"];
    }

    // 기타 북마크
    if (existing.count("기타 북마크") == 0) {
        store_->execute(
            "INSERT INTO bookmarks (parent_id, type, title, position) VALUES (0, 0, '기타 북마크', 1)");
        other_folder_id_ = store_->lastInsertRowId();
    } else {
        other_folder_id_ = existing["기타 북마크"];
    }

    // 메뉴 북마크
    if (existing.count("메뉴 북마크") == 0) {
        store_->execute(
            "INSERT INTO bookmarks (parent_id, type, title, position) VALUES (0, 0, '메뉴 북마크', 2)");
        menu_folder_id_ = store_->lastInsertRowId();
    } else {
        menu_folder_id_ = existing["메뉴 북마크"];
    }
}

// ============================================================
// CRUD
// ============================================================

int64_t BookmarkManager::addBookmark(int64_t parent_id, const std::string& title,
                                      const std::string& url,
                                      const std::vector<std::string>& tags) {
    std::lock_guard lock(mutex_);

    int pos = nextPosition(parent_id);

    store_->execute(
        "INSERT INTO bookmarks (parent_id, type, title, url, position) VALUES (?, ?, ?, ?, ?)",
        {parent_id, static_cast<int64_t>(1), title, url, static_cast<int64_t>(pos)}
    );

    int64_t id = store_->lastInsertRowId();
    if (id <= 0) return -1;

    // 태그 추가
    for (const auto& tag : tags) {
        store_->execute(
            "INSERT OR IGNORE INTO bookmark_tags (bookmark_id, tag) VALUES (?, ?)",
            {id, tag}
        );
    }

    return id;
}

int64_t BookmarkManager::addFolder(int64_t parent_id, const std::string& title) {
    std::lock_guard lock(mutex_);

    int pos = nextPosition(parent_id);

    store_->execute(
        "INSERT INTO bookmarks (parent_id, type, title, position) VALUES (?, ?, ?, ?)",
        {parent_id, static_cast<int64_t>(0), title, static_cast<int64_t>(pos)}
    );

    return store_->lastInsertRowId();
}

int64_t BookmarkManager::addSeparator(int64_t parent_id) {
    std::lock_guard lock(mutex_);

    int pos = nextPosition(parent_id);

    store_->execute(
        "INSERT INTO bookmarks (parent_id, type, title, position) VALUES (?, ?, ?, ?)",
        {parent_id, static_cast<int64_t>(2), std::string("---"), static_cast<int64_t>(pos)}
    );

    return store_->lastInsertRowId();
}

bool BookmarkManager::updateNode(int64_t id, const std::string& title,
                                  const std::string& url,
                                  const std::string& description) {
    std::lock_guard lock(mutex_);

    // 동적 UPDATE 구성
    std::vector<std::string> sets;
    std::vector<DbValue> params;

    if (!title.empty()) {
        sets.push_back("title = ?");
        params.push_back(title);
    }
    if (!url.empty()) {
        sets.push_back("url = ?");
        params.push_back(url);
    }
    if (!description.empty()) {
        sets.push_back("description = ?");
        params.push_back(description);
    }

    if (sets.empty()) return true; // 변경 없음

    sets.push_back("date_modified = datetime('now')");
    params.push_back(id);

    std::string sql = "UPDATE bookmarks SET ";
    for (size_t i = 0; i < sets.size(); ++i) {
        if (i > 0) sql += ", ";
        sql += sets[i];
    }
    sql += " WHERE id = ?";

    return store_->execute(sql, params) >= 0;
}

bool BookmarkManager::removeNode(int64_t id) {
    std::lock_guard lock(mutex_);

    // 폴더인 경우 하위 전부 재귀 삭제
    auto node = getNode(id);
    if (!node) return false;

    if (node->type == BookmarkNodeType::Folder) {
        removeChildrenRecursive(id);
    }

    // 태그 삭제
    store_->execute("DELETE FROM bookmark_tags WHERE bookmark_id = ?", {id});
    // 노드 삭제
    return store_->execute("DELETE FROM bookmarks WHERE id = ?", {id}) >= 0;
}

bool BookmarkManager::moveNode(int64_t id, int64_t new_parent_id, int position) {
    std::lock_guard lock(mutex_);

    if (position < 0) {
        position = nextPosition(new_parent_id);
    }

    return store_->execute(
        "UPDATE bookmarks SET parent_id = ?, position = ?, date_modified = datetime('now') WHERE id = ?",
        {new_parent_id, static_cast<int64_t>(position), id}
    ) >= 0;
}

// ============================================================
// 조회
// ============================================================

std::shared_ptr<BookmarkNode> BookmarkManager::getNode(int64_t id) const {
    auto rows = store_->query("SELECT * FROM bookmarks WHERE id = ?", {id});
    if (rows.empty()) return nullptr;
    return rowToNode(rows.front());
}

std::vector<std::shared_ptr<BookmarkNode>> BookmarkManager::getChildren(int64_t folder_id) const {
    auto rows = store_->query(
        "SELECT * FROM bookmarks WHERE parent_id = ? ORDER BY position", {folder_id});

    std::vector<std::shared_ptr<BookmarkNode>> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.push_back(rowToNode(row));
    }
    return result;
}

std::shared_ptr<BookmarkNode> BookmarkManager::getTree() const {
    // 가상 루트 노드 생성
    auto root = std::make_shared<BookmarkNode>();
    root->id = 0;
    root->type = BookmarkNodeType::Folder;
    root->title = "루트";

    buildTreeRecursive(root);
    return root;
}

int64_t BookmarkManager::totalCount() const {
    auto val = store_->queryScalar("SELECT COUNT(*) FROM bookmarks WHERE type = 1");
    if (val && std::holds_alternative<int64_t>(*val)) {
        return std::get<int64_t>(*val);
    }
    return 0;
}

bool BookmarkManager::incrementVisitCount(int64_t id) {
    return store_->execute(
        "UPDATE bookmarks SET visit_count = visit_count + 1 WHERE id = ?", {id}) >= 0;
}

// ============================================================
// 태그
// ============================================================

bool BookmarkManager::addTag(int64_t bookmark_id, const std::string& tag) {
    return store_->execute(
        "INSERT OR IGNORE INTO bookmark_tags (bookmark_id, tag) VALUES (?, ?)",
        {bookmark_id, tag}
    ) >= 0;
}

bool BookmarkManager::removeTag(int64_t bookmark_id, const std::string& tag) {
    return store_->execute(
        "DELETE FROM bookmark_tags WHERE bookmark_id = ? AND tag = ?",
        {bookmark_id, tag}
    ) >= 0;
}

std::vector<std::string> BookmarkManager::getTags(int64_t bookmark_id) const {
    auto rows = store_->query(
        "SELECT tag FROM bookmark_tags WHERE bookmark_id = ? ORDER BY tag",
        {bookmark_id}
    );

    std::vector<std::string> tags;
    for (const auto& row : rows) {
        auto it = row.find("tag");
        if (it != row.end() && std::holds_alternative<std::string>(it->second)) {
            tags.push_back(std::get<std::string>(it->second));
        }
    }
    return tags;
}

std::vector<std::pair<std::string, int>> BookmarkManager::getAllTags() const {
    auto rows = store_->query(
        "SELECT tag, COUNT(*) as cnt FROM bookmark_tags GROUP BY tag ORDER BY cnt DESC"
    );

    std::vector<std::pair<std::string, int>> result;
    for (const auto& row : rows) {
        auto tag_it = row.find("tag");
        auto cnt_it = row.find("cnt");
        if (tag_it != row.end() && cnt_it != row.end() &&
            std::holds_alternative<std::string>(tag_it->second) &&
            std::holds_alternative<int64_t>(cnt_it->second)) {
            result.emplace_back(
                std::get<std::string>(tag_it->second),
                static_cast<int>(std::get<int64_t>(cnt_it->second))
            );
        }
    }
    return result;
}

std::vector<std::shared_ptr<BookmarkNode>> BookmarkManager::findByTag(const std::string& tag) const {
    auto rows = store_->query(
        "SELECT b.* FROM bookmarks b "
        "JOIN bookmark_tags bt ON b.id = bt.bookmark_id "
        "WHERE bt.tag = ? ORDER BY b.title",
        {tag}
    );

    std::vector<std::shared_ptr<BookmarkNode>> result;
    for (const auto& row : rows) {
        result.push_back(rowToNode(row));
    }
    return result;
}

// ============================================================
// 검색 & 정렬
// ============================================================

std::vector<std::shared_ptr<BookmarkNode>> BookmarkManager::search(
    const std::string& query, int max_results) const {
    std::string pattern = "%" + query + "%";

    std::string sql =
        "SELECT DISTINCT b.* FROM bookmarks b "
        "LEFT JOIN bookmark_tags bt ON b.id = bt.bookmark_id "
        "WHERE b.type = 1 AND ("
        "  b.title LIKE ? OR b.url LIKE ? OR b.description LIKE ? OR bt.tag LIKE ?"
        ") ORDER BY b.visit_count DESC, b.date_modified DESC";

    if (max_results > 0) {
        sql += " LIMIT " + std::to_string(max_results);
    }

    auto rows = store_->query(sql, {pattern, pattern, pattern, pattern});

    std::vector<std::shared_ptr<BookmarkNode>> result;
    for (const auto& row : rows) {
        result.push_back(rowToNode(row));
    }
    return result;
}

std::vector<std::shared_ptr<BookmarkNode>> BookmarkManager::getChildrenSorted(
    int64_t folder_id, BookmarkSortBy sort_by, SortOrder order) const {

    std::string order_clause;
    switch (sort_by) {
        case BookmarkSortBy::Name:
            order_clause = "title"; break;
        case BookmarkSortBy::DateAdded:
            order_clause = "date_added"; break;
        case BookmarkSortBy::DateModified:
            order_clause = "date_modified"; break;
        case BookmarkSortBy::VisitCount:
            order_clause = "visit_count"; break;
        case BookmarkSortBy::Url:
            order_clause = "url"; break;
    }

    order_clause += (order == SortOrder::Ascending) ? " ASC" : " DESC";

    // 폴더를 먼저 정렬
    std::string sql = "SELECT * FROM bookmarks WHERE parent_id = ? "
                      "ORDER BY type ASC, " + order_clause;

    auto rows = store_->query(sql, {folder_id});

    std::vector<std::shared_ptr<BookmarkNode>> result;
    for (const auto& row : rows) {
        result.push_back(rowToNode(row));
    }
    return result;
}

// ============================================================
// 가져오기 / 내보내기
// ============================================================

bool BookmarkManager::exportBookmarks(const std::string& file_path, BookmarkFormat format) const {
    switch (format) {
        case BookmarkFormat::HTML:
            return exportToHtml(file_path);
        case BookmarkFormat::ChromeJson:
            return exportToJson(file_path);
    }
    return false;
}

int BookmarkManager::importBookmarks(const std::string& file_path, BookmarkFormat format,
                                      int64_t target_folder_id) {
    switch (format) {
        case BookmarkFormat::HTML:
            return importFromHtml(file_path, target_folder_id);
        case BookmarkFormat::ChromeJson:
            return importFromJson(file_path, target_folder_id);
    }
    return -1;
}

bool BookmarkManager::isBookmarked(const std::string& url) const {
    auto val = store_->queryScalar(
        "SELECT COUNT(*) FROM bookmarks WHERE type = 1 AND url = ?", {url});
    if (val && std::holds_alternative<int64_t>(*val)) {
        return std::get<int64_t>(*val) > 0;
    }
    return false;
}

std::shared_ptr<BookmarkNode> BookmarkManager::findByUrl(const std::string& url) const {
    auto rows = store_->query(
        "SELECT * FROM bookmarks WHERE type = 1 AND url = ? LIMIT 1", {url});
    if (rows.empty()) return nullptr;
    return rowToNode(rows.front());
}

// ============================================================
// 내부 헬퍼
// ============================================================

std::shared_ptr<BookmarkNode> BookmarkManager::rowToNode(
    const std::unordered_map<std::string,
        std::variant<std::nullptr_t, int64_t, double, std::string, std::vector<uint8_t>>>& row) const {

    auto node = std::make_shared<BookmarkNode>();

    auto getInt = [&](const std::string& key) -> int64_t {
        auto it = row.find(key);
        if (it != row.end() && std::holds_alternative<int64_t>(it->second))
            return std::get<int64_t>(it->second);
        return 0;
    };

    auto getStr = [&](const std::string& key) -> std::string {
        auto it = row.find(key);
        if (it != row.end() && std::holds_alternative<std::string>(it->second))
            return std::get<std::string>(it->second);
        return "";
    };

    node->id = getInt("id");
    node->parent_id = getInt("parent_id");
    node->title = getStr("title");
    node->url = getStr("url");
    node->description = getStr("description");
    node->favicon_url = getStr("favicon_url");
    node->position = static_cast<int>(getInt("position"));
    node->visit_count = static_cast<int>(getInt("visit_count"));

    int type_val = static_cast<int>(getInt("type"));
    switch (type_val) {
        case 0: node->type = BookmarkNodeType::Folder; break;
        case 1: node->type = BookmarkNodeType::Bookmark; break;
        case 2: node->type = BookmarkNodeType::Separator; break;
    }

    // 시간 파싱 (ISO 8601 형태)
    // SQLite의 datetime은 "YYYY-MM-DD HH:MM:SS" 형태
    auto parseTime = [](const std::string& s) -> std::chrono::system_clock::time_point {
        if (s.empty()) return std::chrono::system_clock::now();
        std::tm tm = {};
        std::istringstream ss(s);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (ss.fail()) return std::chrono::system_clock::now();
        return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    };

    node->date_added = parseTime(getStr("date_added"));
    node->date_modified = parseTime(getStr("date_modified"));

    // 태그 로드
    node->tags = getTags(node->id);

    return node;
}

int BookmarkManager::nextPosition(int64_t parent_id) const {
    auto val = store_->queryScalar(
        "SELECT COALESCE(MAX(position), -1) + 1 FROM bookmarks WHERE parent_id = ?",
        {parent_id}
    );
    if (val && std::holds_alternative<int64_t>(*val)) {
        return static_cast<int>(std::get<int64_t>(*val));
    }
    return 0;
}

void BookmarkManager::buildTreeRecursive(std::shared_ptr<BookmarkNode>& node) const {
    auto children = getChildren(node->id);
    node->children = children;

    for (auto& child : node->children) {
        if (child->type == BookmarkNodeType::Folder) {
            buildTreeRecursive(child);
        }
    }
}

void BookmarkManager::removeChildrenRecursive(int64_t folder_id) {
    auto children = getChildren(folder_id);
    for (const auto& child : children) {
        if (child->type == BookmarkNodeType::Folder) {
            removeChildrenRecursive(child->id);
        }
        store_->execute("DELETE FROM bookmark_tags WHERE bookmark_id = ?", {child->id});
        store_->execute("DELETE FROM bookmarks WHERE id = ?", {child->id});
    }
}

// ============================================================
// HTML 내보내기 (Netscape Bookmark 형식)
// ============================================================

bool BookmarkManager::exportToHtml(const std::string& file_path) const {
    std::ofstream ofs(file_path);
    if (!ofs.is_open()) return false;

    ofs << "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n";
    ofs << "<!-- Ordinal Browser 북마크 내보내기 -->\n";
    ofs << "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n";
    ofs << "<TITLE>북마크</TITLE>\n";
    ofs << "<H1>북마크</H1>\n";
    ofs << "<DL><p>\n";

    // 재귀적 HTML 생성
    std::function<void(int64_t, int)> writeFolder = [&](int64_t parent_id, int indent) {
        auto children = getChildren(parent_id);
        std::string pad(indent * 4, ' ');

        for (const auto& child : children) {
            if (child->type == BookmarkNodeType::Folder) {
                auto time_t_val = std::chrono::system_clock::to_time_t(child->date_added);
                ofs << pad << "<DT><H3 ADD_DATE=\"" << time_t_val << "\">"
                    << child->title << "</H3>\n";
                ofs << pad << "<DL><p>\n";
                writeFolder(child->id, indent + 1);
                ofs << pad << "</DL><p>\n";
            } else if (child->type == BookmarkNodeType::Bookmark) {
                auto time_t_val = std::chrono::system_clock::to_time_t(child->date_added);
                ofs << pad << "<DT><A HREF=\"" << child->url
                    << "\" ADD_DATE=\"" << time_t_val << "\"";
                if (!child->tags.empty()) {
                    ofs << " TAGS=\"" << joinTags(child->tags) << "\"";
                }
                ofs << ">" << child->title << "</A>\n";
                if (!child->description.empty()) {
                    ofs << pad << "<DD>" << child->description << "\n";
                }
            } else if (child->type == BookmarkNodeType::Separator) {
                ofs << pad << "<HR>\n";
            }
        }
    };

    writeFolder(0, 1);
    ofs << "</DL><p>\n";

    return true;
}

// ============================================================
// Chrome JSON 내보내기
// ============================================================

bool BookmarkManager::exportToJson(const std::string& file_path) const {
    std::ofstream ofs(file_path);
    if (!ofs.is_open()) return false;

    // JSON 재귀 생성
    std::function<void(int64_t, int)> writeNode = [&](int64_t parent_id, int indent) {
        auto children = getChildren(parent_id);
        std::string pad(indent * 2, ' ');

        for (size_t i = 0; i < children.size(); ++i) {
            const auto& child = children[i];
            ofs << pad << "{\n";

            auto time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                child->date_added.time_since_epoch()).count();

            ofs << pad << "  \"date_added\": \"" << time_us << "\",\n";
            ofs << pad << "  \"id\": \"" << child->id << "\",\n";
            ofs << pad << "  \"name\": \"" << child->title << "\",\n";

            if (child->type == BookmarkNodeType::Folder) {
                ofs << pad << "  \"type\": \"folder\",\n";
                ofs << pad << "  \"children\": [\n";
                writeNode(child->id, indent + 2);
                ofs << pad << "  ]\n";
            } else {
                ofs << pad << "  \"type\": \"url\",\n";
                ofs << pad << "  \"url\": \"" << child->url << "\"\n";
            }

            ofs << pad << "}";
            if (i + 1 < children.size()) ofs << ",";
            ofs << "\n";
        }
    };

    ofs << "{\n";
    ofs << "  \"checksum\": \"\",\n";
    ofs << "  \"roots\": {\n";
    ofs << "    \"bookmark_bar\": {\n";
    ofs << "      \"children\": [\n";
    writeNode(toolbar_folder_id_, 4);
    ofs << "      ],\n";
    ofs << "      \"name\": \"북마크 바\",\n";
    ofs << "      \"type\": \"folder\"\n";
    ofs << "    },\n";
    ofs << "    \"other\": {\n";
    ofs << "      \"children\": [\n";
    writeNode(other_folder_id_, 4);
    ofs << "      ],\n";
    ofs << "      \"name\": \"기타 북마크\",\n";
    ofs << "      \"type\": \"folder\"\n";
    ofs << "    }\n";
    ofs << "  },\n";
    ofs << "  \"version\": 1\n";
    ofs << "}\n";

    return true;
}

// ============================================================
// HTML 가져오기
// ============================================================

int BookmarkManager::importFromHtml(const std::string& file_path, int64_t target_folder_id) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) return -1;

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    int count = 0;
    std::stack<int64_t> folder_stack;
    folder_stack.push(target_folder_id == 0 ? other_folder_id_ : target_folder_id);

    // 줄 단위 파싱
    std::istringstream stream(content);
    std::string line;

    // 정규식 패턴
    std::regex href_re(R"(<A\s+HREF="([^"]*)"[^>]*>([^<]*)</A>)", std::regex::icase);
    std::regex folder_re(R"(<H3[^>]*>([^<]*)</H3>)", std::regex::icase);
    std::regex dl_close_re(R"(</DL>)", std::regex::icase);
    std::regex tags_re(R"(TAGS="([^"]*)")", std::regex::icase);

    while (std::getline(stream, line)) {
        std::smatch match;

        // 폴더 시작
        if (std::regex_search(line, match, folder_re)) {
            std::string folder_name = match[1].str();
            int64_t folder_id = addFolder(folder_stack.top(), folder_name);
            if (folder_id > 0) {
                folder_stack.push(folder_id);
            }
            continue;
        }

        // 폴더 종료 </DL>
        if (std::regex_search(line, match, dl_close_re)) {
            if (folder_stack.size() > 1) {
                folder_stack.pop();
            }
            continue;
        }

        // 북마크 <A HREF="...">...</A>
        if (std::regex_search(line, match, href_re)) {
            std::string url = match[1].str();
            std::string title = match[2].str();

            // 태그 추출
            std::vector<std::string> tags;
            std::smatch tags_match;
            if (std::regex_search(line, tags_match, tags_re)) {
                tags = parseTags(tags_match[1].str());
            }

            int64_t id = addBookmark(folder_stack.top(), title, url, tags);
            if (id > 0) ++count;
        }
    }

    return count;
}

// ============================================================
// Chrome JSON 가져오기
// ============================================================

int BookmarkManager::importFromJson(const std::string& file_path, int64_t target_folder_id) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) return -1;

    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());

    int count = 0;
    int64_t target = target_folder_id == 0 ? other_folder_id_ : target_folder_id;

    // 간이 JSON 파서: "name", "url", "type", "children" 키를 추적
    // 재귀적 처리를 위해 스택 기반
    struct ParseContext {
        int64_t folder_id;
        int brace_depth;
    };

    std::stack<ParseContext> ctx_stack;
    ctx_stack.push({target, 0});

    // 단순 토큰 기반 파싱
    size_t pos = 0;
    std::string current_name;
    std::string current_url;
    std::string current_type;
    bool in_children = false;

    auto findValue = [&](const std::string& key, size_t start) -> std::string {
        std::string search = "\"" + key + "\"";
        auto key_pos = content.find(search, start);
        if (key_pos == std::string::npos) return "";

        auto colon = content.find(':', key_pos + search.size());
        if (colon == std::string::npos) return "";

        // 값의 시작 찾기 (공백 건너뛰기)
        auto val_start = content.find('"', colon + 1);
        if (val_start == std::string::npos) return "";

        auto val_end = content.find('"', val_start + 1);
        if (val_end == std::string::npos) return "";

        return content.substr(val_start + 1, val_end - val_start - 1);
    };

    // children 배열 내의 각 객체를 순회
    std::function<int(const std::string&, size_t, size_t, int64_t)> parseArray;
    parseArray = [&](const std::string& data, size_t start, size_t end, int64_t folder_id) -> int {
        int cnt = 0;
        size_t p = start;

        while (p < end) {
            // 다음 객체 시작 찾기
            auto obj_start = data.find('{', p);
            if (obj_start == std::string::npos || obj_start >= end) break;

            // 대응 } 찾기 (중첩 고려)
            int depth = 1;
            size_t obj_end = obj_start + 1;
            while (obj_end < data.size() && depth > 0) {
                if (data[obj_end] == '{') ++depth;
                else if (data[obj_end] == '}') --depth;
                ++obj_end;
            }

            // 객체 내에서 name, type, url 추출
            std::string name = findValue("name", obj_start);
            std::string type = findValue("type", obj_start);
            std::string url = findValue("url", obj_start);

            if (type == "folder") {
                int64_t fid = addFolder(folder_id, name);
                if (fid > 0) {
                    // children 배열 찾기
                    std::string children_key = "\"children\"";
                    auto ch_pos = data.find(children_key, obj_start);
                    if (ch_pos != std::string::npos && ch_pos < obj_end) {
                        auto bracket = data.find('[', ch_pos);
                        if (bracket != std::string::npos && bracket < obj_end) {
                            // 대응 ] 찾기
                            int bd = 1;
                            size_t bracket_end = bracket + 1;
                            while (bracket_end < data.size() && bd > 0) {
                                if (data[bracket_end] == '[') ++bd;
                                else if (data[bracket_end] == ']') --bd;
                                ++bracket_end;
                            }
                            cnt += parseArray(data, bracket + 1, bracket_end - 1, fid);
                        }
                    }
                }
            } else if (type == "url" && !url.empty()) {
                int64_t id = addBookmark(folder_id, name, url);
                if (id > 0) ++cnt;
            }

            p = obj_end;
        }

        return cnt;
    };

    // "roots" 내의 "bookmark_bar"와 "other" 파싱
    for (const auto& root_key : {"bookmark_bar", "other", "synced"}) {
        std::string key = "\"" + std::string(root_key) + "\"";
        auto root_pos = content.find(key);
        if (root_pos == std::string::npos) continue;

        // children 배열 찾기
        std::string children_key = "\"children\"";
        auto ch_pos = content.find(children_key, root_pos);
        if (ch_pos == std::string::npos) continue;

        auto bracket = content.find('[', ch_pos);
        if (bracket == std::string::npos) continue;

        int bd = 1;
        size_t bracket_end = bracket + 1;
        while (bracket_end < content.size() && bd > 0) {
            if (content[bracket_end] == '[') ++bd;
            else if (content[bracket_end] == ']') --bd;
            ++bracket_end;
        }

        count += parseArray(content, bracket + 1, bracket_end - 1, target);
    }

    return count;
}

// ============================================================
// 태그 유틸리티
// ============================================================

std::vector<std::string> BookmarkManager::parseTags(const std::string& tag_str) {
    std::vector<std::string> tags;
    std::istringstream ss(tag_str);
    std::string tag;
    while (std::getline(ss, tag, ',')) {
        // 공백 제거
        tag.erase(0, tag.find_first_not_of(' '));
        tag.erase(tag.find_last_not_of(' ') + 1);
        if (!tag.empty()) {
            tags.push_back(tag);
        }
    }
    return tags;
}

std::string BookmarkManager::joinTags(const std::vector<std::string>& tags) {
    std::string result;
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) result += ",";
        result += tags[i];
    }
    return result;
}

} // namespace ordinal::data
