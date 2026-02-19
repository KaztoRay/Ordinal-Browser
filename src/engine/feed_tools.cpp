#include "feed_tools.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QImage>
#include <QPainter>
#include <QBuffer>
#include <optional>
#include <iostream>

namespace Ordinal {
namespace Engine {

// ============================================================
// FeedItem / Feed serialization
// ============================================================

QJsonObject FeedItem::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["feedId"] = feedId;
    obj["title"] = title;
    obj["link"] = link.toString();
    obj["description"] = description;
    obj["author"] = author;
    obj["published"] = published.toString(Qt::ISODate);
    obj["read"] = read;
    obj["starred"] = starred;
    return obj;
}

QJsonObject Feed::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["title"] = title;
    obj["feedUrl"] = feedUrl.toString();
    obj["siteUrl"] = siteUrl.toString();
    obj["description"] = description;
    obj["lastUpdated"] = lastUpdated.toString(Qt::ISODate);
    obj["unreadCount"] = unreadCount;
    obj["enabled"] = enabled;
    return obj;
}

// ============================================================
// FeedReader
// ============================================================

FeedReader::FeedReader(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_dbPath(storagePath + "/feeds.db")
    , m_network(new QNetworkAccessManager(this))
{
    QDir().mkpath(storagePath);
    initDatabase();
}

FeedReader::~FeedReader()
{
    if (m_db.isOpen()) m_db.close();
}

void FeedReader::initDatabase()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "feeds");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        std::cerr << "[FeedReader] DB 열기 실패" << std::endl;
        return;
    }

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS feeds (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT,
            feed_url TEXT NOT NULL UNIQUE,
            site_url TEXT,
            description TEXT,
            favicon TEXT,
            last_updated TEXT,
            enabled INTEGER NOT NULL DEFAULT 1
        )
    )");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS feed_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            feed_id INTEGER NOT NULL,
            title TEXT,
            link TEXT,
            description TEXT,
            author TEXT,
            published TEXT,
            read INTEGER NOT NULL DEFAULT 0,
            starred INTEGER NOT NULL DEFAULT 0,
            FOREIGN KEY (feed_id) REFERENCES feeds(id) ON DELETE CASCADE,
            UNIQUE(feed_id, link)
        )
    )");

    q.exec("CREATE INDEX IF NOT EXISTS idx_items_feed ON feed_items(feed_id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_read ON feed_items(read)");
}

int64_t FeedReader::subscribe(const QUrl& feedUrl)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO feeds (feed_url, title) VALUES (?, ?)");
    q.addBindValue(feedUrl.toString());
    q.addBindValue(feedUrl.host());
    if (!q.exec()) return -1;

    int64_t feedId = q.lastInsertId().toLongLong();
    refreshFeed(feedId);

    Feed feed;
    feed.id = feedId;
    feed.feedUrl = feedUrl;
    feed.title = feedUrl.host();
    emit feedAdded(feed);
    return feedId;
}

bool FeedReader::unsubscribe(int64_t feedId)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM feed_items WHERE feed_id = ?");
    q.addBindValue(feedId);
    q.exec();

    q.prepare("DELETE FROM feeds WHERE id = ?");
    q.addBindValue(feedId);
    if (q.exec()) {
        emit feedRemoved(feedId);
        return true;
    }
    return false;
}

void FeedReader::refreshFeed(int64_t feedId)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT feed_url FROM feeds WHERE id = ?");
    q.addBindValue(feedId);
    q.exec();
    if (!q.next()) return;

    QUrl feedUrl(q.value(0).toString());
    QNetworkRequest request(feedUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "OrdinalBrowser/1.3.0 FeedReader");

    auto* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, feedId, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            parseFeed(feedId, reply->readAll());
        } else {
            emit fetchError(feedId, reply->errorString());
        }
        reply->deleteLater();
    });
}

void FeedReader::refreshAll()
{
    for (const auto& feed : allFeeds()) {
        if (feed.enabled) refreshFeed(feed.id);
    }
}

QList<QUrl> FeedReader::detectFeeds(const QString& html, const QUrl& pageUrl) const
{
    QList<QUrl> feeds;

    // <link rel="alternate" type="application/rss+xml" href="...">
    QRegularExpression linkRx(
        "<link[^>]*type\\s*=\\s*[\"']application/(rss|atom)\\+xml[\"'][^>]*href\\s*=\\s*[\"']([^\"']+)[\"']",
        QRegularExpression::CaseInsensitiveOption);

    auto it = linkRx.globalMatch(html);
    while (it.hasNext()) {
        QString href = it.next().captured(2);
        QUrl feedUrl = pageUrl.resolved(QUrl(href));
        if (feedUrl.isValid()) feeds.append(feedUrl);
    }

    // href가 먼저 오는 경우도 처리
    QRegularExpression linkRx2(
        "<link[^>]*href\\s*=\\s*[\"']([^\"']+)[\"'][^>]*type\\s*=\\s*[\"']application/(rss|atom)\\+xml[\"']",
        QRegularExpression::CaseInsensitiveOption);

    auto it2 = linkRx2.globalMatch(html);
    while (it2.hasNext()) {
        QString href = it2.next().captured(1);
        QUrl feedUrl = pageUrl.resolved(QUrl(href));
        if (feedUrl.isValid() && !feeds.contains(feedUrl)) feeds.append(feedUrl);
    }

    return feeds;
}

QList<Feed> FeedReader::allFeeds() const
{
    QList<Feed> feeds;
    QSqlQuery q(m_db);
    q.exec("SELECT * FROM feeds ORDER BY title");
    while (q.next()) {
        Feed f;
        f.id = q.value("id").toLongLong();
        f.title = q.value("title").toString();
        f.feedUrl = QUrl(q.value("feed_url").toString());
        f.siteUrl = QUrl(q.value("site_url").toString());
        f.description = q.value("description").toString();
        f.lastUpdated = QDateTime::fromString(q.value("last_updated").toString(), Qt::ISODate);
        f.enabled = q.value("enabled").toBool();

        // unread count
        QSqlQuery cq(m_db);
        cq.prepare("SELECT COUNT(*) FROM feed_items WHERE feed_id = ? AND read = 0");
        cq.addBindValue(f.id);
        cq.exec();
        cq.next();
        f.unreadCount = cq.value(0).toInt();

        feeds.append(f);
    }
    return feeds;
}

QList<FeedItem> FeedReader::getItems(int64_t feedId, int limit) const
{
    QList<FeedItem> items;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM feed_items WHERE feed_id = ? ORDER BY published DESC LIMIT ?");
    q.addBindValue(feedId);
    q.addBindValue(limit);
    q.exec();
    while (q.next()) {
        FeedItem item;
        item.id = q.value("id").toLongLong();
        item.feedId = q.value("feed_id").toLongLong();
        item.title = q.value("title").toString();
        item.link = QUrl(q.value("link").toString());
        item.description = q.value("description").toString();
        item.author = q.value("author").toString();
        item.published = QDateTime::fromString(q.value("published").toString(), Qt::ISODate);
        item.read = q.value("read").toBool();
        item.starred = q.value("starred").toBool();
        items.append(item);
    }
    return items;
}

QList<FeedItem> FeedReader::getUnread(int limit) const
{
    QList<FeedItem> items;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM feed_items WHERE read = 0 ORDER BY published DESC LIMIT ?");
    q.addBindValue(limit);
    q.exec();
    while (q.next()) {
        FeedItem item;
        item.id = q.value("id").toLongLong();
        item.feedId = q.value("feed_id").toLongLong();
        item.title = q.value("title").toString();
        item.link = QUrl(q.value("link").toString());
        item.published = QDateTime::fromString(q.value("published").toString(), Qt::ISODate);
        item.read = false;
        item.starred = q.value("starred").toBool();
        items.append(item);
    }
    return items;
}

QList<FeedItem> FeedReader::getStarred(int limit) const
{
    QList<FeedItem> items;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM feed_items WHERE starred = 1 ORDER BY published DESC LIMIT ?");
    q.addBindValue(limit);
    q.exec();
    while (q.next()) {
        FeedItem item;
        item.id = q.value("id").toLongLong();
        item.title = q.value("title").toString();
        item.link = QUrl(q.value("link").toString());
        item.starred = true;
        items.append(item);
    }
    return items;
}

int FeedReader::totalUnread() const
{
    QSqlQuery q(m_db);
    q.exec("SELECT COUNT(*) FROM feed_items WHERE read = 0");
    q.next();
    return q.value(0).toInt();
}

bool FeedReader::markAsRead(int64_t itemId)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE feed_items SET read = 1 WHERE id = ?");
    q.addBindValue(itemId);
    return q.exec();
}

bool FeedReader::markAllAsRead(int64_t feedId)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE feed_items SET read = 1 WHERE feed_id = ?");
    q.addBindValue(feedId);
    return q.exec();
}

bool FeedReader::toggleStar(int64_t itemId)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE feed_items SET starred = NOT starred WHERE id = ?");
    q.addBindValue(itemId);
    return q.exec();
}

void FeedReader::parseFeed(int64_t feedId, const QByteArray& data)
{
    QXmlStreamReader xml(data);
    int newCount = 0;

    while (xml.readNextStartElement()) {
        if (xml.name() == u"rss" || xml.name() == u"channel") {
            parseRss(feedId, xml);
            break;
        } else if (xml.name() == u"feed") {
            parseAtom(feedId, xml);
            break;
        } else {
            xml.skipCurrentElement();
        }
    }

    // 업데이트 시간
    QSqlQuery q(m_db);
    q.prepare("UPDATE feeds SET last_updated = datetime('now') WHERE id = ?");
    q.addBindValue(feedId);
    q.exec();
}

void FeedReader::parseRss(int64_t feedId, QXmlStreamReader& xml)
{
    int newCount = 0;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"item") {
            FeedItem item;
            item.feedId = feedId;

            while (!(xml.isEndElement() && xml.name() == u"item")) {
                xml.readNext();
                if (xml.isStartElement()) {
                    if (xml.name() == u"title") item.title = xml.readElementText();
                    else if (xml.name() == u"link") item.link = QUrl(xml.readElementText());
                    else if (xml.name() == u"description") item.description = xml.readElementText();
                    else if (xml.name() == u"author" || xml.name() == u"creator")
                        item.author = xml.readElementText();
                    else if (xml.name() == u"pubDate")
                        item.published = QDateTime::fromString(xml.readElementText().trimmed(), Qt::RFC2822Date);
                    else xml.skipCurrentElement();
                }
            }

            if (insertItem(feedId, item) >= 0) newCount++;
        } else if (xml.isStartElement() && xml.name() == u"title") {
            QString title = xml.readElementText();
            QSqlQuery q(m_db);
            q.prepare("UPDATE feeds SET title = ? WHERE id = ? AND title = feed_url");
            q.addBindValue(title);
            q.addBindValue(feedId);
            q.exec();
        }
    }

    if (newCount > 0) emit newItems(feedId, newCount);
}

void FeedReader::parseAtom(int64_t feedId, QXmlStreamReader& xml)
{
    int newCount = 0;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"entry") {
            FeedItem item;
            item.feedId = feedId;

            while (!(xml.isEndElement() && xml.name() == u"entry")) {
                xml.readNext();
                if (xml.isStartElement()) {
                    if (xml.name() == u"title") item.title = xml.readElementText();
                    else if (xml.name() == u"link") {
                        item.link = QUrl(xml.attributes().value("href").toString());
                        xml.skipCurrentElement();
                    }
                    else if (xml.name() == u"summary" || xml.name() == u"content")
                        item.description = xml.readElementText();
                    else if (xml.name() == u"author") {
                        while (!(xml.isEndElement() && xml.name() == u"author")) {
                            xml.readNext();
                            if (xml.isStartElement() && xml.name() == u"name")
                                item.author = xml.readElementText();
                        }
                    }
                    else if (xml.name() == u"published" || xml.name() == u"updated")
                        item.published = QDateTime::fromString(xml.readElementText(), Qt::ISODate);
                    else xml.skipCurrentElement();
                }
            }

            if (insertItem(feedId, item) >= 0) newCount++;
        }
    }

    if (newCount > 0) emit newItems(feedId, newCount);
}

int64_t FeedReader::insertItem(int64_t feedId, const FeedItem& item)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO feed_items (feed_id, title, link, description, author, published) "
              "VALUES (?, ?, ?, ?, ?, ?)");
    q.addBindValue(feedId);
    q.addBindValue(item.title);
    q.addBindValue(item.link.toString());
    q.addBindValue(item.description);
    q.addBindValue(item.author);
    q.addBindValue(item.published.toString(Qt::ISODate));
    if (!q.exec()) return -1;
    return q.lastInsertId().toLongLong();
}

// ============================================================
// NoteItem
// ============================================================

QJsonObject NoteItem::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["title"] = title;
    obj["content"] = content;
    obj["url"] = associatedUrl.toString();
    obj["created"] = created.toString(Qt::ISODate);
    obj["modified"] = modified.toString(Qt::ISODate);
    obj["color"] = color;
    return obj;
}

// ============================================================
// NotePad
// ============================================================

NotePad::NotePad(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_dbPath(storagePath + "/notes.db")
{
    QDir().mkpath(storagePath);
    initDatabase();
}

NotePad::~NotePad()
{
    if (m_db.isOpen()) m_db.close();
}

void NotePad::initDatabase()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "notes");
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) return;

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS notes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL DEFAULT '',
            content TEXT NOT NULL DEFAULT '',
            url TEXT,
            created TEXT NOT NULL DEFAULT (datetime('now')),
            modified TEXT NOT NULL DEFAULT (datetime('now')),
            color TEXT DEFAULT '#ffffff'
        )
    )");
}

int64_t NotePad::createNote(const QString& title, const QString& content, const QUrl& url)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO notes (title, content, url) VALUES (?, ?, ?)");
    q.addBindValue(title.isEmpty() ? "새 메모" : title);
    q.addBindValue(content);
    q.addBindValue(url.toString());
    if (!q.exec()) return -1;

    int64_t id = q.lastInsertId().toLongLong();
    auto note = getNote(id);
    if (note) emit noteCreated(*note);
    return id;
}

bool NotePad::updateNote(int64_t id, const QString& content)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE notes SET content = ?, modified = datetime('now') WHERE id = ?");
    q.addBindValue(content);
    q.addBindValue(id);
    return q.exec();
}

bool NotePad::updateTitle(int64_t id, const QString& title)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE notes SET title = ?, modified = datetime('now') WHERE id = ?");
    q.addBindValue(title);
    q.addBindValue(id);
    return q.exec();
}

bool NotePad::removeNote(int64_t id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM notes WHERE id = ?");
    q.addBindValue(id);
    if (q.exec()) {
        emit noteRemoved(id);
        return true;
    }
    return false;
}

bool NotePad::setColor(int64_t id, const QString& color)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE notes SET color = ? WHERE id = ?");
    q.addBindValue(color);
    q.addBindValue(id);
    return q.exec();
}

QList<NoteItem> NotePad::allNotes() const
{
    QList<NoteItem> notes;
    QSqlQuery q(m_db);
    q.exec("SELECT * FROM notes ORDER BY modified DESC");
    while (q.next()) {
        NoteItem n;
        n.id = q.value("id").toLongLong();
        n.title = q.value("title").toString();
        n.content = q.value("content").toString();
        n.associatedUrl = QUrl(q.value("url").toString());
        n.created = QDateTime::fromString(q.value("created").toString(), Qt::ISODate);
        n.modified = QDateTime::fromString(q.value("modified").toString(), Qt::ISODate);
        n.color = q.value("color").toString();
        notes.append(n);
    }
    return notes;
}

QList<NoteItem> NotePad::searchNotes(const QString& query) const
{
    QList<NoteItem> notes;
    QSqlQuery q(m_db);
    QString pattern = "%" + query + "%";
    q.prepare("SELECT * FROM notes WHERE title LIKE ? OR content LIKE ? ORDER BY modified DESC");
    q.addBindValue(pattern);
    q.addBindValue(pattern);
    q.exec();
    while (q.next()) {
        NoteItem n;
        n.id = q.value("id").toLongLong();
        n.title = q.value("title").toString();
        n.content = q.value("content").toString();
        n.associatedUrl = QUrl(q.value("url").toString());
        n.modified = QDateTime::fromString(q.value("modified").toString(), Qt::ISODate);
        notes.append(n);
    }
    return notes;
}

QList<NoteItem> NotePad::notesForUrl(const QUrl& url) const
{
    QList<NoteItem> notes;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM notes WHERE url = ? ORDER BY modified DESC");
    q.addBindValue(url.toString());
    q.exec();
    while (q.next()) {
        NoteItem n;
        n.id = q.value("id").toLongLong();
        n.title = q.value("title").toString();
        n.content = q.value("content").toString();
        n.associatedUrl = QUrl(q.value("url").toString());
        notes.append(n);
    }
    return notes;
}

std::optional<NoteItem> NotePad::getNote(int64_t id) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM notes WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    if (q.next()) {
        NoteItem n;
        n.id = q.value("id").toLongLong();
        n.title = q.value("title").toString();
        n.content = q.value("content").toString();
        n.associatedUrl = QUrl(q.value("url").toString());
        n.created = QDateTime::fromString(q.value("created").toString(), Qt::ISODate);
        n.modified = QDateTime::fromString(q.value("modified").toString(), Qt::ISODate);
        n.color = q.value("color").toString();
        return n;
    }
    return std::nullopt;
}

int NotePad::totalCount() const
{
    QSqlQuery q(m_db);
    q.exec("SELECT COUNT(*) FROM notes");
    q.next();
    return q.value(0).toInt();
}

// ============================================================
// CommandPalette
// ============================================================

CommandPalette::CommandPalette(QObject* parent)
    : QObject(parent)
{
}

void CommandPalette::registerCommand(const CommandEntry& cmd)
{
    m_commands.append(cmd);
}

void CommandPalette::removeCommand(const QString& id)
{
    m_commands.removeIf([&id](const CommandEntry& c) { return c.id == id; });
}

QList<CommandEntry> CommandPalette::search(const QString& query) const
{
    if (query.isEmpty()) return m_commands;

    QList<CommandEntry> results;
    QString lower = query.toLower();
    for (const auto& cmd : m_commands) {
        if (cmd.name.toLower().contains(lower) ||
            cmd.category.toLower().contains(lower) ||
            cmd.id.toLower().contains(lower)) {
            results.append(cmd);
        }
    }
    return results;
}

void CommandPalette::execute(const QString& id)
{
    for (const auto& cmd : m_commands) {
        if (cmd.id == id && cmd.action) {
            cmd.action();
            emit commandExecuted(id);
            return;
        }
    }
}

// ============================================================
// QRGenerator
// ============================================================

QRGenerator::QRGenerator(QObject* parent)
    : QObject(parent)
{
}

QString QRGenerator::generateSvg(const QString& data, int moduleSize)
{
    auto matrix = encode(data);
    int size = matrix.size();
    int imgSize = size * moduleSize;

    QString svg = QString(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 %1 %1' width='%1' height='%1'>"
        "<rect width='%1' height='%1' fill='white'/>"
    ).arg(imgSize);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (matrix[y][x]) {
                svg += QString("<rect x='%1' y='%2' width='%3' height='%3' fill='black'/>")
                    .arg(x * moduleSize).arg(y * moduleSize).arg(moduleSize);
            }
        }
    }

    svg += "</svg>";
    return svg;
}

QByteArray QRGenerator::generatePng(const QString& data, int size)
{
    auto matrix = encode(data);
    int matSize = matrix.size();
    int moduleSize = size / matSize;

    QImage img(size, size, QImage::Format_RGB32);
    img.fill(Qt::white);

    QPainter painter(&img);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);

    for (int y = 0; y < matSize; ++y) {
        for (int x = 0; x < matSize; ++x) {
            if (matrix[y][x]) {
                painter.drawRect(x * moduleSize, y * moduleSize, moduleSize, moduleSize);
            }
        }
    }
    painter.end();

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "PNG");
    return bytes;
}

QString QRGenerator::generateHtmlPopup(const QUrl& url)
{
    QString svg = generateSvg(url.toString(), 6);
    return QString(R"(
<!DOCTYPE html>
<html>
<head><title>QR 코드 — %1</title>
<style>
body { font-family: -apple-system, sans-serif; text-align: center; padding: 20px; background: #f5f5f5; }
.qr { margin: 20px auto; }
.url { word-break: break-all; color: #666; font-size: 12px; max-width: 300px; margin: 0 auto; }
h2 { font-weight: 400; font-size: 18px; }
</style>
</head>
<body>
<h2>📱 QR 코드</h2>
<div class="qr">%2</div>
<p class="url">%1</p>
</body>
</html>
    )").arg(url.toString(), svg);
}

QVector<QVector<bool>> QRGenerator::encode(const QString& data)
{
    // 간이 QR (Version 1, 21x21 모듈, 에러 보정 없음)
    int size = 21;
    QVector<QVector<bool>> matrix(size, QVector<bool>(size, false));

    // Finder patterns (3개)
    addFinderPattern(matrix, 0, 0);
    addFinderPattern(matrix, 0, size - 7);
    addFinderPattern(matrix, size - 7, 0);

    // 타이밍 패턴
    for (int i = 8; i < size - 8; ++i) {
        matrix[6][i] = (i % 2 == 0);
        matrix[i][6] = (i % 2 == 0);
    }

    // 데이터를 간단히 비트맵으로 인코딩
    QByteArray bytes = data.toUtf8();
    int bitIdx = 0;
    for (int col = size - 1; col >= 1; col -= 2) {
        if (col == 6) col = 5; // 타이밍 패턴 건너뛰기
        for (int row = 0; row < size; ++row) {
            for (int c = 0; c < 2; ++c) {
                int x = col - c;
                int y = (col / 2 % 2 == 0) ? row : (size - 1 - row);
                if (y < 0 || y >= size || x < 0 || x >= size) continue;
                if (matrix[y][x]) continue; // finder/timing 영역 건너뛰기

                // 데이터 영역인지 확인
                bool inFinder = (y < 9 && x < 9) || (y < 9 && x > size - 9) || (y > size - 9 && x < 9);
                if (inFinder) continue;

                if (bitIdx < bytes.size() * 8) {
                    bool bit = (bytes[bitIdx / 8] >> (7 - bitIdx % 8)) & 1;
                    matrix[y][x] = bit;
                    bitIdx++;
                }
            }
        }
    }

    return matrix;
}

void QRGenerator::addFinderPattern(QVector<QVector<bool>>& matrix, int row, int col)
{
    for (int r = 0; r < 7; ++r) {
        for (int c = 0; c < 7; ++c) {
            bool border = (r == 0 || r == 6 || c == 0 || c == 6);
            bool inner = (r >= 2 && r <= 4 && c >= 2 && c <= 4);
            matrix[row + r][col + c] = border || inner;
        }
    }
    // 세퍼레이터 (흰색 줄)
    for (int i = 0; i < 8; ++i) {
        if (row + 7 < matrix.size()) matrix[row + 7][col + qMin(i, 6)] = false;
        if (col + 7 < matrix[0].size()) matrix[row + qMin(i, 6)][col + 7] = false;
    }
}

} // namespace Engine
} // namespace Ordinal
