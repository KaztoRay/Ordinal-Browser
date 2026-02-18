#include "data_manager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QTimer>
#include <QVariant>
#include <iostream>

namespace Ordinal {
namespace Engine {

// ============================================================
// BookmarkItem serialization
// ============================================================

QJsonObject BookmarkItem::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["parentId"] = parentId;
    obj["title"] = title;
    obj["url"] = url.toString();
    obj["isFolder"] = isFolder;
    obj["position"] = position;
    obj["created"] = created.toString(Qt::ISODate);
    obj["modified"] = modified.toString(Qt::ISODate);
    return obj;
}

BookmarkItem BookmarkItem::fromJson(const QJsonObject& obj)
{
    BookmarkItem item;
    item.id = obj["id"].toInteger(-1);
    item.parentId = obj["parentId"].toInteger(0);
    item.title = obj["title"].toString();
    item.url = QUrl(obj["url"].toString());
    item.isFolder = obj["isFolder"].toBool();
    item.position = obj["position"].toInt();
    item.created = QDateTime::fromString(obj["created"].toString(), Qt::ISODate);
    item.modified = QDateTime::fromString(obj["modified"].toString(), Qt::ISODate);
    return item;
}

// ============================================================
// BookmarkManager
// ============================================================

BookmarkManager::BookmarkManager(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_dbPath(storagePath + "/bookmarks.db")
{
    QDir().mkpath(storagePath);
    initDatabase();
}

BookmarkManager::~BookmarkManager()
{
    if (m_db.isOpen()) m_db.close();
}

void BookmarkManager::initDatabase()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "bookmarks");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        std::cerr << "[Bookmarks] DB 열기 실패: "
                  << m_db.lastError().text().toStdString() << std::endl;
        return;
    }

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("PRAGMA foreign_keys=ON");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS bookmarks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            parent_id INTEGER NOT NULL DEFAULT 0,
            title TEXT NOT NULL,
            url TEXT,
            is_folder INTEGER NOT NULL DEFAULT 0,
            position INTEGER NOT NULL DEFAULT 0,
            created TEXT NOT NULL DEFAULT (datetime('now')),
            modified TEXT NOT NULL DEFAULT (datetime('now')),
            favicon TEXT,
            FOREIGN KEY (parent_id) REFERENCES bookmarks(id) ON DELETE CASCADE
        )
    )");

    q.exec("CREATE INDEX IF NOT EXISTS idx_bookmarks_parent ON bookmarks(parent_id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_bookmarks_url ON bookmarks(url)");

    createDefaultFolders();
}

void BookmarkManager::createDefaultFolders()
{
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM bookmarks WHERE id IN (1, 2)");
    q.exec();
    q.next();
    if (q.value(0).toInt() >= 2) return;

    q.prepare("INSERT OR IGNORE INTO bookmarks (id, parent_id, title, is_folder, position) "
              "VALUES (1, 0, '북마크 바', 1, 0)");
    q.exec();
    q.prepare("INSERT OR IGNORE INTO bookmarks (id, parent_id, title, is_folder, position) "
              "VALUES (2, 0, '기타 북마크', 1, 1)");
    q.exec();

    m_bookmarkBarId = 1;
    m_otherBookmarksId = 2;
}

int64_t BookmarkManager::addBookmark(const QString& title, const QUrl& url, int64_t parentId)
{
    if (parentId == 0) parentId = m_bookmarkBarId;

    QSqlQuery q(m_db);
    q.prepare("INSERT INTO bookmarks (parent_id, title, url, is_folder, position, created, modified) "
              "VALUES (?, ?, ?, 0, ?, datetime('now'), datetime('now'))");
    q.addBindValue(parentId);
    q.addBindValue(title);
    q.addBindValue(url.toString());
    q.addBindValue(nextPosition(parentId));

    if (!q.exec()) return -1;

    int64_t id = q.lastInsertId().toLongLong();
    auto item = getBookmark(id);
    if (item) emit bookmarkAdded(*item);
    return id;
}

int64_t BookmarkManager::addFolder(const QString& name, int64_t parentId)
{
    if (parentId == 0) parentId = m_bookmarkBarId;

    QSqlQuery q(m_db);
    q.prepare("INSERT INTO bookmarks (parent_id, title, is_folder, position, created, modified) "
              "VALUES (?, ?, 1, ?, datetime('now'), datetime('now'))");
    q.addBindValue(parentId);
    q.addBindValue(name);
    q.addBindValue(nextPosition(parentId));

    if (!q.exec()) return -1;

    int64_t id = q.lastInsertId().toLongLong();
    auto item = getBookmark(id);
    if (item) emit bookmarkAdded(*item);
    return id;
}

bool BookmarkManager::removeBookmark(int64_t id)
{
    if (id == m_bookmarkBarId || id == m_otherBookmarksId) return false;

    QSqlQuery q(m_db);
    q.prepare("DELETE FROM bookmarks WHERE id = ?");
    q.addBindValue(id);
    if (q.exec()) {
        emit bookmarkRemoved(id);
        return true;
    }
    return false;
}

bool BookmarkManager::updateBookmark(int64_t id, const QString& title, const QUrl& url)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE bookmarks SET title = ?, url = ?, modified = datetime('now') WHERE id = ?");
    q.addBindValue(title);
    q.addBindValue(url.toString());
    q.addBindValue(id);

    if (q.exec()) {
        auto item = getBookmark(id);
        if (item) emit bookmarkUpdated(*item);
        return true;
    }
    return false;
}

bool BookmarkManager::moveBookmark(int64_t id, int64_t newParentId, int position)
{
    if (position < 0) position = nextPosition(newParentId);

    QSqlQuery q(m_db);
    q.prepare("UPDATE bookmarks SET parent_id = ?, position = ?, modified = datetime('now') WHERE id = ?");
    q.addBindValue(newParentId);
    q.addBindValue(position);
    q.addBindValue(id);

    if (q.exec()) {
        emit bookmarkMoved(id, newParentId);
        return true;
    }
    return false;
}

std::optional<BookmarkItem> BookmarkManager::getBookmark(int64_t id) const
{
    auto items = queryItems("SELECT * FROM bookmarks WHERE id = ?", {id});
    if (items.isEmpty()) return std::nullopt;
    return items.first();
}

QList<BookmarkItem> BookmarkManager::getChildren(int64_t parentId) const
{
    return queryItems("SELECT * FROM bookmarks WHERE parent_id = ? ORDER BY position",
                      {parentId});
}

QList<BookmarkItem> BookmarkManager::search(const QString& query) const
{
    QString pattern = "%" + query + "%";
    return queryItems(
        "SELECT * FROM bookmarks WHERE (title LIKE ? OR url LIKE ?) AND is_folder = 0 "
        "ORDER BY modified DESC LIMIT 50",
        {pattern, pattern});
}

QList<BookmarkItem> BookmarkManager::getRecent(int limit) const
{
    return queryItems(
        "SELECT * FROM bookmarks WHERE is_folder = 0 ORDER BY created DESC LIMIT ?",
        {limit});
}

bool BookmarkManager::isBookmarked(const QUrl& url) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM bookmarks WHERE url = ?");
    q.addBindValue(url.toString());
    q.exec();
    q.next();
    return q.value(0).toInt() > 0;
}

std::optional<BookmarkItem> BookmarkManager::findByUrl(const QUrl& url) const
{
    auto items = queryItems("SELECT * FROM bookmarks WHERE url = ? LIMIT 1",
                            {url.toString()});
    if (items.isEmpty()) return std::nullopt;
    return items.first();
}

QJsonArray BookmarkManager::exportToJson() const
{
    QJsonArray arr;
    auto items = queryItems("SELECT * FROM bookmarks ORDER BY parent_id, position");
    for (const auto& item : items) {
        arr.append(item.toJson());
    }
    return arr;
}

bool BookmarkManager::importFromJson(const QJsonArray& data, int64_t parentId)
{
    for (const auto& val : data) {
        auto item = BookmarkItem::fromJson(val.toObject());
        if (item.isFolder) {
            addFolder(item.title, parentId);
        } else {
            addBookmark(item.title, item.url, parentId);
        }
    }
    return true;
}

bool BookmarkManager::importFromHtml(const QString& htmlPath)
{
    QFile file(htmlPath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QString html = QString::fromUtf8(file.readAll());
    // 간단한 Netscape Bookmark HTML 파싱
    QRegularExpression linkRx("<A\\s+HREF=\"([^\"]+)\"[^>]*>([^<]+)</A>",
                               QRegularExpression::CaseInsensitiveOption);
    auto it = linkRx.globalMatch(html);
    while (it.hasNext()) {
        auto match = it.next();
        addBookmark(match.captured(2), QUrl(match.captured(1)));
    }
    return true;
}

int BookmarkManager::totalCount() const
{
    QSqlQuery q(m_db);
    q.exec("SELECT COUNT(*) FROM bookmarks WHERE is_folder = 0");
    q.next();
    return q.value(0).toInt();
}

int BookmarkManager::nextPosition(int64_t parentId) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT COALESCE(MAX(position), -1) + 1 FROM bookmarks WHERE parent_id = ?");
    q.addBindValue(parentId);
    q.exec();
    q.next();
    return q.value(0).toInt();
}

QList<BookmarkItem> BookmarkManager::queryItems(const QString& sql, const QVariantList& params) const
{
    QList<BookmarkItem> items;
    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto& p : params) q.addBindValue(p);
    q.exec();

    while (q.next()) {
        BookmarkItem item;
        item.id = q.value("id").toLongLong();
        item.parentId = q.value("parent_id").toLongLong();
        item.title = q.value("title").toString();
        item.url = QUrl(q.value("url").toString());
        item.isFolder = q.value("is_folder").toBool();
        item.position = q.value("position").toInt();
        item.created = QDateTime::fromString(q.value("created").toString(), Qt::ISODate);
        item.modified = QDateTime::fromString(q.value("modified").toString(), Qt::ISODate);
        item.favicon = q.value("favicon").toString();
        items.append(item);
    }
    return items;
}

// ============================================================
// HistoryEntry serialization
// ============================================================

QJsonObject HistoryEntry::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["url"] = url.toString();
    obj["title"] = title;
    obj["visitTime"] = visitTime.toString(Qt::ISODate);
    obj["visitCount"] = visitCount;
    return obj;
}

// ============================================================
// HistoryManager
// ============================================================

HistoryManager::HistoryManager(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_dbPath(storagePath + "/history.db")
{
    QDir().mkpath(storagePath);
    initDatabase();
}

HistoryManager::~HistoryManager()
{
    if (m_db.isOpen()) m_db.close();
}

void HistoryManager::initDatabase()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "history");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        std::cerr << "[History] DB 열기 실패: "
                  << m_db.lastError().text().toStdString() << std::endl;
        return;
    }

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            url TEXT NOT NULL,
            title TEXT,
            visit_time TEXT NOT NULL DEFAULT (datetime('now')),
            visit_count INTEGER NOT NULL DEFAULT 1,
            favicon TEXT
        )
    )");

    q.exec("CREATE INDEX IF NOT EXISTS idx_history_url ON history(url)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_history_time ON history(visit_time DESC)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_history_title ON history(title)");
}

void HistoryManager::addVisit(const QUrl& url, const QString& title)
{
    // ordinal:// 내부 페이지는 기록하지 않음
    if (url.scheme() == "ordinal") return;

    QSqlQuery q(m_db);
    // 이미 있으면 visit_count 증가 + title 업데이트
    q.prepare("SELECT id, visit_count FROM history WHERE url = ? ORDER BY visit_time DESC LIMIT 1");
    q.addBindValue(url.toString());
    q.exec();

    if (q.next()) {
        int64_t existId = q.value(0).toLongLong();
        int count = q.value(1).toInt();
        QSqlQuery update(m_db);
        update.prepare("UPDATE history SET title = ?, visit_count = ?, visit_time = datetime('now') WHERE id = ?");
        update.addBindValue(title);
        update.addBindValue(count + 1);
        update.addBindValue(existId);
        update.exec();
    } else {
        q.prepare("INSERT INTO history (url, title, visit_time, visit_count) "
                  "VALUES (?, ?, datetime('now'), 1)");
        q.addBindValue(url.toString());
        q.addBindValue(title);
        q.exec();
    }

    HistoryEntry entry;
    entry.url = url;
    entry.title = title;
    entry.visitTime = QDateTime::currentDateTime();
    emit historyAdded(entry);
}

bool HistoryManager::removeEntry(int64_t id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM history WHERE id = ?");
    q.addBindValue(id);
    return q.exec();
}

bool HistoryManager::removeByUrl(const QUrl& url)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM history WHERE url = ?");
    q.addBindValue(url.toString());
    return q.exec();
}

bool HistoryManager::clearAll()
{
    QSqlQuery q(m_db);
    bool ok = q.exec("DELETE FROM history");
    if (ok) emit historyCleared();
    return ok;
}

bool HistoryManager::clearRange(const QDateTime& from, const QDateTime& to)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM history WHERE visit_time BETWEEN ? AND ?");
    q.addBindValue(from.toString(Qt::ISODate));
    q.addBindValue(to.toString(Qt::ISODate));
    return q.exec();
}

QList<HistoryEntry> HistoryManager::search(const QString& query, int limit) const
{
    QString pattern = "%" + query + "%";
    return queryEntries(
        "SELECT * FROM history WHERE title LIKE ? OR url LIKE ? "
        "ORDER BY visit_time DESC LIMIT ?",
        {pattern, pattern, limit});
}

QList<HistoryEntry> HistoryManager::getRecent(int limit) const
{
    return queryEntries("SELECT * FROM history ORDER BY visit_time DESC LIMIT ?", {limit});
}

QList<HistoryEntry> HistoryManager::getByDate(const QDate& date) const
{
    QString dayStart = QDateTime(date, QTime(0, 0, 0)).toString(Qt::ISODate);
    QString dayEnd = QDateTime(date, QTime(23, 59, 59)).toString(Qt::ISODate);
    return queryEntries(
        "SELECT * FROM history WHERE visit_time BETWEEN ? AND ? ORDER BY visit_time DESC",
        {dayStart, dayEnd});
}

QList<HistoryEntry> HistoryManager::getMostVisited(int limit) const
{
    return queryEntries("SELECT * FROM history ORDER BY visit_count DESC LIMIT ?", {limit});
}

int HistoryManager::totalCount() const
{
    QSqlQuery q(m_db);
    q.exec("SELECT COUNT(*) FROM history");
    q.next();
    return q.value(0).toInt();
}

QList<HistoryEntry> HistoryManager::suggest(const QString& prefix, int limit) const
{
    QString pattern = prefix + "%";
    QString containsPattern = "%" + prefix + "%";
    return queryEntries(
        "SELECT * FROM history WHERE url LIKE ? OR title LIKE ? "
        "ORDER BY visit_count DESC, visit_time DESC LIMIT ?",
        {pattern, containsPattern, limit});
}

QList<HistoryEntry> HistoryManager::queryEntries(const QString& sql, const QVariantList& params) const
{
    QList<HistoryEntry> entries;
    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto& p : params) q.addBindValue(p);
    q.exec();

    while (q.next()) {
        HistoryEntry entry;
        entry.id = q.value("id").toLongLong();
        entry.url = QUrl(q.value("url").toString());
        entry.title = q.value("title").toString();
        entry.visitTime = QDateTime::fromString(q.value("visit_time").toString(), Qt::ISODate);
        entry.visitCount = q.value("visit_count").toInt();
        entry.favicon = q.value("favicon").toString();
        entries.append(entry);
    }
    return entries;
}

// ============================================================
// TabState / SessionData serialization
// ============================================================

QJsonObject TabState::toJson() const
{
    QJsonObject obj;
    obj["url"] = url.toString();
    obj["title"] = title;
    obj["pinned"] = pinned;
    obj["muted"] = muted;
    obj["scrollPosition"] = scrollPosition;
    obj["lastAccessed"] = lastAccessed.toString(Qt::ISODate);
    return obj;
}

TabState TabState::fromJson(const QJsonObject& obj)
{
    TabState state;
    state.url = QUrl(obj["url"].toString());
    state.title = obj["title"].toString();
    state.pinned = obj["pinned"].toBool();
    state.muted = obj["muted"].toBool();
    state.scrollPosition = obj["scrollPosition"].toInt();
    state.lastAccessed = QDateTime::fromString(obj["lastAccessed"].toString(), Qt::ISODate);
    return state;
}

QJsonObject SessionData::toJson() const
{
    QJsonObject obj;
    QJsonArray tabArray;
    for (const auto& tab : tabs) {
        tabArray.append(tab.toJson());
    }
    obj["tabs"] = tabArray;
    obj["activeTabIndex"] = activeTabIndex;
    obj["savedAt"] = savedAt.toString(Qt::ISODate);
    return obj;
}

SessionData SessionData::fromJson(const QJsonObject& obj)
{
    SessionData data;
    auto tabArray = obj["tabs"].toArray();
    for (const auto& val : tabArray) {
        data.tabs.append(TabState::fromJson(val.toObject()));
    }
    data.activeTabIndex = obj["activeTabIndex"].toInt();
    data.savedAt = QDateTime::fromString(obj["savedAt"].toString(), Qt::ISODate);
    return data;
}

// ============================================================
// SessionManager
// ============================================================

SessionManager::SessionManager(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_sessionPath(storagePath + "/session.json")
{
    QDir().mkpath(storagePath);
}

SessionManager::~SessionManager()
{
    stopAutoSave();
}

void SessionManager::saveSession(const SessionData& session)
{
    SessionData data = session;
    data.savedAt = QDateTime::currentDateTime();

    QFile file(m_sessionPath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(data.toJson());
        file.write(doc.toJson(QJsonDocument::Compact));
        emit sessionSaved();
    }
}

SessionData SessionManager::loadSession() const
{
    QFile file(m_sessionPath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return SessionData::fromJson(doc.object());
}

bool SessionManager::hasSession() const
{
    return QFile::exists(m_sessionPath);
}

void SessionManager::clearSession()
{
    QFile::remove(m_sessionPath);
}

void SessionManager::setAutoSaveInterval(int seconds)
{
    if (m_autoSaveTimer) {
        m_autoSaveTimer->setInterval(seconds * 1000);
    }
}

void SessionManager::startAutoSave(std::function<SessionData()> sessionGetter)
{
    m_sessionGetter = std::move(sessionGetter);

    if (!m_autoSaveTimer) {
        m_autoSaveTimer = new QTimer(this);
        m_autoSaveTimer->setInterval(30000); // 30초 기본
        connect(m_autoSaveTimer, &QTimer::timeout, this, [this]() {
            if (m_sessionGetter) {
                saveSession(m_sessionGetter());
            }
        });
    }

    m_autoSaveTimer->start();
}

void SessionManager::stopAutoSave()
{
    if (m_autoSaveTimer) {
        m_autoSaveTimer->stop();
    }
}

} // namespace Engine
} // namespace Ordinal
