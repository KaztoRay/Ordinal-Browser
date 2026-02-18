#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QUrl>
#include <QList>
#include <QSqlDatabase>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QRegularExpression>
#include <functional>
#include <optional>

namespace Ordinal {
namespace Engine {

// ============================================================
// BookmarkItem — 북마크/폴더 노드
// ============================================================
struct BookmarkItem {
    int64_t id = -1;
    int64_t parentId = 0;  // 0 = root
    QString title;
    QUrl url;               // 폴더일 경우 비어있음
    bool isFolder = false;
    int position = 0;
    QDateTime created;
    QDateTime modified;
    QString favicon;         // base64 favicon data

    bool isValid() const { return id >= 0; }
    QJsonObject toJson() const;
    static BookmarkItem fromJson(const QJsonObject& obj);
};

// ============================================================
// BookmarkManager — SQLite 기반 북마크 관리
// ============================================================
class BookmarkManager : public QObject {
    Q_OBJECT

public:
    explicit BookmarkManager(const QString& storagePath, QObject* parent = nullptr);
    ~BookmarkManager() override;

    // CRUD
    int64_t addBookmark(const QString& title, const QUrl& url, int64_t parentId = 0);
    int64_t addFolder(const QString& name, int64_t parentId = 0);
    bool removeBookmark(int64_t id);
    bool updateBookmark(int64_t id, const QString& title, const QUrl& url);
    bool moveBookmark(int64_t id, int64_t newParentId, int position = -1);

    // 검색/조회
    std::optional<BookmarkItem> getBookmark(int64_t id) const;
    QList<BookmarkItem> getChildren(int64_t parentId = 0) const;
    QList<BookmarkItem> search(const QString& query) const;
    QList<BookmarkItem> getRecent(int limit = 20) const;
    bool isBookmarked(const QUrl& url) const;
    std::optional<BookmarkItem> findByUrl(const QUrl& url) const;

    // 특수 폴더
    int64_t bookmarkBarId() const { return m_bookmarkBarId; }
    int64_t otherBookmarksId() const { return m_otherBookmarksId; }

    // Import/Export
    QJsonArray exportToJson() const;
    bool importFromJson(const QJsonArray& data, int64_t parentId = 0);
    bool importFromHtml(const QString& htmlPath);

    int totalCount() const;

signals:
    void bookmarkAdded(const BookmarkItem& item);
    void bookmarkRemoved(int64_t id);
    void bookmarkUpdated(const BookmarkItem& item);
    void bookmarkMoved(int64_t id, int64_t newParentId);

private:
    void initDatabase();
    void createDefaultFolders();
    int nextPosition(int64_t parentId) const;
    QList<BookmarkItem> queryItems(const QString& sql, const QVariantList& params = {}) const;

    QSqlDatabase m_db;
    QString m_dbPath;
    int64_t m_bookmarkBarId = 1;
    int64_t m_otherBookmarksId = 2;
};

// ============================================================
// HistoryEntry — 방문 기록 항목
// ============================================================
struct HistoryEntry {
    int64_t id = -1;
    QUrl url;
    QString title;
    QDateTime visitTime;
    int visitCount = 1;
    QString favicon;

    QJsonObject toJson() const;
};

// ============================================================
// HistoryManager — SQLite 기반 방문 기록
// ============================================================
class HistoryManager : public QObject {
    Q_OBJECT

public:
    explicit HistoryManager(const QString& storagePath, QObject* parent = nullptr);
    ~HistoryManager() override;

    // 기록 추가/삭제
    void addVisit(const QUrl& url, const QString& title);
    bool removeEntry(int64_t id);
    bool removeByUrl(const QUrl& url);
    bool clearAll();
    bool clearRange(const QDateTime& from, const QDateTime& to);

    // 검색/조회
    QList<HistoryEntry> search(const QString& query, int limit = 50) const;
    QList<HistoryEntry> getRecent(int limit = 50) const;
    QList<HistoryEntry> getByDate(const QDate& date) const;
    QList<HistoryEntry> getMostVisited(int limit = 20) const;
    int totalCount() const;

    // Autocomplete support
    QList<HistoryEntry> suggest(const QString& prefix, int limit = 8) const;

signals:
    void historyAdded(const HistoryEntry& entry);
    void historyCleared();

private:
    void initDatabase();
    QList<HistoryEntry> queryEntries(const QString& sql, const QVariantList& params = {}) const;

    QSqlDatabase m_db;
    QString m_dbPath;
};

// ============================================================
// SessionManager — 탭 상태 저장/복원
// ============================================================
struct TabState {
    QUrl url;
    QString title;
    bool pinned = false;
    bool muted = false;
    int scrollPosition = 0;
    QDateTime lastAccessed;

    QJsonObject toJson() const;
    static TabState fromJson(const QJsonObject& obj);
};

struct SessionData {
    QList<TabState> tabs;
    int activeTabIndex = 0;
    QDateTime savedAt;

    QJsonObject toJson() const;
    static SessionData fromJson(const QJsonObject& obj);
};

class SessionManager : public QObject {
    Q_OBJECT

public:
    explicit SessionManager(const QString& storagePath, QObject* parent = nullptr);
    ~SessionManager() override;

    // 세션 저장/복원
    void saveSession(const SessionData& session);
    SessionData loadSession() const;
    bool hasSession() const;
    void clearSession();

    // 자동 저장 (주기적)
    void setAutoSaveInterval(int seconds);
    void startAutoSave(std::function<SessionData()> sessionGetter);
    void stopAutoSave();

signals:
    void sessionSaved();
    void sessionRestored(const SessionData& session);

private:
    QString m_sessionPath;
    QTimer* m_autoSaveTimer = nullptr;
    std::function<SessionData()> m_sessionGetter;
};

} // namespace Engine
} // namespace Ordinal
