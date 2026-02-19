#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QDateTime>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QNetworkAccessManager>
#include <QXmlStreamReader>

namespace Ordinal {
namespace Engine {

// ============================================================
// FeedItem — RSS/Atom 피드 항목
// ============================================================
struct FeedItem {
    int64_t id = -1;
    int64_t feedId = -1;
    QString title;
    QUrl link;
    QString description;
    QString author;
    QDateTime published;
    bool read = false;
    bool starred = false;

    QJsonObject toJson() const;
};

// ============================================================
// Feed — RSS/Atom 피드 소스
// ============================================================
struct Feed {
    int64_t id = -1;
    QString title;
    QUrl feedUrl;
    QUrl siteUrl;
    QString description;
    QString favicon;
    QDateTime lastUpdated;
    int unreadCount = 0;
    bool enabled = true;

    QJsonObject toJson() const;
};

// ============================================================
// FeedReader — RSS/Atom 피드 리더
// ============================================================
class FeedReader : public QObject {
    Q_OBJECT

public:
    explicit FeedReader(const QString& storagePath, QObject* parent = nullptr);
    ~FeedReader() override;

    // 피드 구독
    int64_t subscribe(const QUrl& feedUrl);
    bool unsubscribe(int64_t feedId);

    // 새로고침
    void refreshFeed(int64_t feedId);
    void refreshAll();

    // 페이지에서 피드 자동 감지
    QList<QUrl> detectFeeds(const QString& html, const QUrl& pageUrl) const;

    // 조회
    QList<Feed> allFeeds() const;
    QList<FeedItem> getItems(int64_t feedId, int limit = 50) const;
    QList<FeedItem> getUnread(int limit = 100) const;
    QList<FeedItem> getStarred(int limit = 100) const;
    int totalUnread() const;

    // 상태
    bool markAsRead(int64_t itemId);
    bool markAllAsRead(int64_t feedId);
    bool toggleStar(int64_t itemId);

signals:
    void feedAdded(const Feed& feed);
    void feedRemoved(int64_t feedId);
    void feedUpdated(const Feed& feed);
    void newItems(int64_t feedId, int count);
    void fetchError(int64_t feedId, const QString& error);

private:
    void initDatabase();
    void parseFeed(int64_t feedId, const QByteArray& data);
    void parseRss(int64_t feedId, QXmlStreamReader& xml);
    void parseAtom(int64_t feedId, QXmlStreamReader& xml);
    int64_t insertItem(int64_t feedId, const FeedItem& item);

    QSqlDatabase m_db;
    QString m_dbPath;
    QNetworkAccessManager* m_network;
};

// ============================================================
// NoteItem — 메모 항목
// ============================================================
struct NoteItem {
    int64_t id = -1;
    QString title;
    QString content;
    QUrl associatedUrl;  // 메모 작성 시 현재 페이지 URL
    QDateTime created;
    QDateTime modified;
    QString color;       // 메모 배경색

    QJsonObject toJson() const;
};

// ============================================================
// NotePad — 사이드바 내장 메모장
// ============================================================
class NotePad : public QObject {
    Q_OBJECT

public:
    explicit NotePad(const QString& storagePath, QObject* parent = nullptr);
    ~NotePad() override;

    int64_t createNote(const QString& title = "", const QString& content = "",
                        const QUrl& url = QUrl());
    bool updateNote(int64_t id, const QString& content);
    bool updateTitle(int64_t id, const QString& title);
    bool removeNote(int64_t id);
    bool setColor(int64_t id, const QString& color);

    QList<NoteItem> allNotes() const;
    QList<NoteItem> searchNotes(const QString& query) const;
    QList<NoteItem> notesForUrl(const QUrl& url) const;
    std::optional<NoteItem> getNote(int64_t id) const;
    int totalCount() const;

signals:
    void noteCreated(const NoteItem& note);
    void noteUpdated(const NoteItem& note);
    void noteRemoved(int64_t id);

private:
    void initDatabase();

    QSqlDatabase m_db;
    QString m_dbPath;
};

// ============================================================
// CommandPalette — 명령 팔레트 (Ctrl+Shift+P)
// ============================================================
struct CommandEntry {
    QString id;
    QString name;
    QString shortcut;
    QString category;   // "탭", "보안", "도구", "보기"
    std::function<void()> action;
};

class CommandPalette : public QObject {
    Q_OBJECT

public:
    explicit CommandPalette(QObject* parent = nullptr);

    void registerCommand(const CommandEntry& cmd);
    void removeCommand(const QString& id);
    QList<CommandEntry> search(const QString& query) const;
    QList<CommandEntry> allCommands() const { return m_commands; }
    void execute(const QString& id);

signals:
    void commandExecuted(const QString& id);

private:
    QList<CommandEntry> m_commands;
};

// ============================================================
// QRGenerator — QR 코드 생성기 (SVG)
// ============================================================
class QRGenerator : public QObject {
    Q_OBJECT

public:
    explicit QRGenerator(QObject* parent = nullptr);

    // QR 코드를 SVG 문자열로 생성
    static QString generateSvg(const QString& data, int moduleSize = 4);

    // QR 코드를 PNG 바이트로 생성
    static QByteArray generatePng(const QString& data, int size = 256);

    // HTML 팝업 생성
    static QString generateHtmlPopup(const QUrl& url);

private:
    // 간이 QR 인코딩 (최대 약 100자)
    static QVector<QVector<bool>> encode(const QString& data);
    static void addFinderPattern(QVector<QVector<bool>>& matrix, int row, int col);
};

} // namespace Engine
} // namespace Ordinal
