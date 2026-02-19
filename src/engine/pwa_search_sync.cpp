#include "pwa_search_sync.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <iostream>

namespace Ordinal {
namespace Engine {

// ============================================================
// PWAApp
// ============================================================

QJsonObject PWAApp::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["shortName"] = shortName;
    obj["startUrl"] = startUrl.toString();
    obj["scope"] = scope.toString();
    obj["display"] = display;
    obj["themeColor"] = themeColor.name();
    obj["backgroundColor"] = backgroundColor.name();
    obj["installed"] = installed.toString(Qt::ISODate);
    obj["pinned"] = pinned;
    return obj;
}

PWAApp PWAApp::fromManifest(const QJsonObject& manifest, const QUrl& baseUrl)
{
    PWAApp app;
    app.name = manifest["name"].toString();
    app.shortName = manifest["short_name"].toString(app.name);
    app.startUrl = baseUrl.resolved(QUrl(manifest["start_url"].toString("/")));
    app.scope = baseUrl.resolved(QUrl(manifest["scope"].toString("/")));
    app.display = manifest["display"].toString("browser");
    app.themeColor = QColor(manifest["theme_color"].toString("#ffffff"));
    app.backgroundColor = QColor(manifest["background_color"].toString("#ffffff"));

    auto icons = manifest["icons"].toArray();
    for (const auto& icon : icons) {
        app.icons.append(baseUrl.resolved(QUrl(icon.toObject()["src"].toString())).toString());
    }

    return app;
}

// ============================================================
// PWAManager
// ============================================================

PWAManager::PWAManager(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_dbPath(storagePath + "/pwa.db")
    , m_network(new QNetworkAccessManager(this))
{
    QDir().mkpath(storagePath);
    initDatabase();
}

PWAManager::~PWAManager()
{
    if (m_db.isOpen()) m_db.close();
}

void PWAManager::initDatabase()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "pwa");
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) return;

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS pwa_apps (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            short_name TEXT,
            start_url TEXT NOT NULL UNIQUE,
            scope TEXT,
            display TEXT DEFAULT 'browser',
            theme_color TEXT,
            bg_color TEXT,
            icons TEXT,
            installed TEXT NOT NULL DEFAULT (datetime('now')),
            pinned INTEGER DEFAULT 0
        )
    )");
}

void PWAManager::detectPWA(const QString& html, const QUrl& pageUrl)
{
    QRegularExpression manifestRx(
        "<link[^>]*rel\\s*=\\s*[\"']manifest[\"'][^>]*href\\s*=\\s*[\"']([^\"']+)[\"']",
        QRegularExpression::CaseInsensitiveOption);

    auto match = manifestRx.match(html);
    if (match.hasMatch()) {
        QUrl manifestUrl = pageUrl.resolved(QUrl(match.captured(1)));
        emit pwaDetected(manifestUrl, pageUrl);
    }

    // href가 먼저 오는 경우
    QRegularExpression manifestRx2(
        "<link[^>]*href\\s*=\\s*[\"']([^\"']+)[\"'][^>]*rel\\s*=\\s*[\"']manifest[\"']",
        QRegularExpression::CaseInsensitiveOption);

    auto match2 = manifestRx2.match(html);
    if (match2.hasMatch() && !match.hasMatch()) {
        QUrl manifestUrl = pageUrl.resolved(QUrl(match2.captured(1)));
        emit pwaDetected(manifestUrl, pageUrl);
    }
}

int64_t PWAManager::installApp(const PWAApp& app)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO pwa_apps (name, short_name, start_url, scope, display, "
              "theme_color, bg_color, icons) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(app.name);
    q.addBindValue(app.shortName);
    q.addBindValue(app.startUrl.toString());
    q.addBindValue(app.scope.toString());
    q.addBindValue(app.display);
    q.addBindValue(app.themeColor.name());
    q.addBindValue(app.backgroundColor.name());
    q.addBindValue(app.icons.join(";"));
    if (!q.exec()) return -1;

    int64_t id = q.lastInsertId().toLongLong();
    PWAApp installed = app;
    installed.id = id;
    installed.installed = QDateTime::currentDateTime();
    emit appInstalled(installed);
    return id;
}

bool PWAManager::removeApp(int64_t id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM pwa_apps WHERE id = ?");
    q.addBindValue(id);
    if (q.exec()) {
        emit appRemoved(id);
        return true;
    }
    return false;
}

bool PWAManager::pinApp(int64_t id, bool pinned)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE pwa_apps SET pinned = ? WHERE id = ?");
    q.addBindValue(pinned ? 1 : 0);
    q.addBindValue(id);
    return q.exec();
}

QList<PWAApp> PWAManager::installedApps() const
{
    QList<PWAApp> apps;
    QSqlQuery q(m_db);
    q.exec("SELECT * FROM pwa_apps ORDER BY name");
    while (q.next()) {
        PWAApp app;
        app.id = q.value("id").toLongLong();
        app.name = q.value("name").toString();
        app.shortName = q.value("short_name").toString();
        app.startUrl = QUrl(q.value("start_url").toString());
        app.scope = QUrl(q.value("scope").toString());
        app.display = q.value("display").toString();
        app.themeColor = QColor(q.value("theme_color").toString());
        app.backgroundColor = QColor(q.value("bg_color").toString());
        app.installed = QDateTime::fromString(q.value("installed").toString(), Qt::ISODate);
        app.pinned = q.value("pinned").toBool();
        apps.append(app);
    }
    return apps;
}

bool PWAManager::isInstalled(const QUrl& startUrl) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM pwa_apps WHERE start_url = ?");
    q.addBindValue(startUrl.toString());
    q.exec();
    q.next();
    return q.value(0).toInt() > 0;
}

void PWAManager::fetchManifest(const QUrl& manifestUrl, const QUrl& pageUrl)
{
    QNetworkRequest request(manifestUrl);
    auto* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pageUrl]() {
        if (reply->error() == QNetworkReply::NoError) {
            auto doc = QJsonDocument::fromJson(reply->readAll());
            if (doc.isObject()) {
                auto app = PWAApp::fromManifest(doc.object(), pageUrl);
                emit manifestParsed(app);
            }
        }
        reply->deleteLater();
    });
}

// ============================================================
// WebScraper
// ============================================================

WebScraper::WebScraper(QObject* parent)
    : QObject(parent)
{
}

QString WebScraper::extractBySelector(const QString& selector)
{
    return QString(R"(
(function() {
    var els = document.querySelectorAll('%1');
    return Array.from(els).map(function(e) { return e.textContent.trim(); }).filter(Boolean);
})();
    )").arg(selector);
}

QString WebScraper::extractAllLinks()
{
    return R"(
(function() {
    return Array.from(document.querySelectorAll('a[href]')).map(function(a) {
        return { text: a.textContent.trim(), href: a.href };
    }).filter(function(l) { return l.href && l.href.startsWith('http'); });
})();
    )";
}

QString WebScraper::extractAllImages()
{
    return R"(
(function() {
    return Array.from(document.querySelectorAll('img[src]')).map(function(img) {
        return { alt: img.alt, src: img.src, width: img.naturalWidth, height: img.naturalHeight };
    });
})();
    )";
}

QString WebScraper::extractMetadata()
{
    return R"(
(function() {
    var meta = {};
    meta.title = document.title;
    meta.description = (document.querySelector('meta[name="description"]') || {}).content || '';
    meta.canonical = (document.querySelector('link[rel="canonical"]') || {}).href || '';
    meta.language = document.documentElement.lang || '';

    // Open Graph
    document.querySelectorAll('meta[property^="og:"]').forEach(function(m) {
        meta['og:' + m.getAttribute('property').slice(3)] = m.content;
    });

    // Twitter Card
    document.querySelectorAll('meta[name^="twitter:"]').forEach(function(m) {
        meta['twitter:' + m.getAttribute('name').slice(8)] = m.content;
    });

    return meta;
})();
    )";
}

QString WebScraper::extractTables()
{
    return R"(
(function() {
    var tables = [];
    document.querySelectorAll('table').forEach(function(table, idx) {
        var rows = [];
        table.querySelectorAll('tr').forEach(function(tr) {
            var cells = [];
            tr.querySelectorAll('th, td').forEach(function(cell) {
                cells.push(cell.textContent.trim());
            });
            if (cells.length > 0) rows.push(cells);
        });
        if (rows.length > 0) tables.push({ index: idx, rows: rows });
    });
    return tables;
})();
    )";
}

QString WebScraper::extractStructuredData()
{
    return R"(
(function() {
    var data = [];
    // JSON-LD
    document.querySelectorAll('script[type="application/ld+json"]').forEach(function(s) {
        try { data.push(JSON.parse(s.textContent)); } catch(e) {}
    });
    return data;
})();
    )";
}

void WebScraper::runScript(QWebEnginePage* page, const QString& js)
{
    if (!page) {
        emit scrapeError("페이지가 없습니다");
        return;
    }

    page->runJavaScript(js, [this](const QVariant& result) {
        emit dataExtracted(QJsonDocument::fromVariant(result).toJson(QJsonDocument::Indented));
    });
}

// ============================================================
// SearchEngineEntry
// ============================================================

QJsonObject SearchEngineEntry::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["searchUrl"] = searchUrl.toString();
    obj["suggestUrl"] = suggestUrl.toString();
    obj["iconUrl"] = iconUrl.toString();
    obj["shortcut"] = shortcut;
    obj["isDefault"] = isDefault;
    obj["builtin"] = builtin;
    return obj;
}

QUrl SearchEngineEntry::buildSearchUrl(const QString& query) const
{
    QString urlStr = searchUrl.toString();
    urlStr.replace("{query}", QUrl::toPercentEncoding(query));
    return QUrl(urlStr);
}

// ============================================================
// SearchEngineManager
// ============================================================

SearchEngineManager::SearchEngineManager(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_storagePath(storagePath)
{
    loadEngines();
    if (m_engines.isEmpty()) initBuiltinEngines();
}

void SearchEngineManager::initBuiltinEngines()
{
    m_engines.clear();

    auto add = [this](const QString& id, const QString& name, const QString& url,
                       const QString& suggest, const QString& shortcut) {
        SearchEngineEntry e;
        e.id = id; e.name = name;
        e.searchUrl = QUrl(url);
        if (!suggest.isEmpty()) e.suggestUrl = QUrl(suggest);
        e.shortcut = shortcut;
        e.builtin = true;
        e.isDefault = (id == m_defaultId);
        m_engines.append(e);
    };

    add("duckduckgo", "DuckDuckGo",
        "https://duckduckgo.com/?q={query}",
        "https://duckduckgo.com/ac/?q={query}&type=list", "d");

    add("google", "Google",
        "https://www.google.com/search?q={query}",
        "https://suggestqueries.google.com/complete/search?client=firefox&q={query}", "g");

    add("bing", "Bing",
        "https://www.bing.com/search?q={query}",
        "https://api.bing.com/osjson.aspx?query={query}", "b");

    add("brave", "Brave Search",
        "https://search.brave.com/search?q={query}", "", "br");

    add("startpage", "Startpage",
        "https://www.startpage.com/do/dsearch?query={query}", "", "sp");

    add("ecosia", "Ecosia",
        "https://www.ecosia.org/search?q={query}", "", "ec");

    add("naver", "네이버",
        "https://search.naver.com/search.naver?query={query}",
        "https://ac.search.naver.com/nx/ac?q={query}&st=100", "n");

    add("daum", "다음",
        "https://search.daum.net/search?q={query}", "", "da");

    add("youtube", "YouTube",
        "https://www.youtube.com/results?search_query={query}",
        "https://suggestqueries.google.com/complete/search?client=youtube&q={query}", "yt");

    add("github", "GitHub",
        "https://github.com/search?q={query}", "", "gh");

    add("wikipedia", "Wikipedia",
        "https://en.wikipedia.org/w/index.php?search={query}", "", "w");

    saveEngines();
}

void SearchEngineManager::addEngine(const SearchEngineEntry& engine)
{
    m_engines.append(engine);
    saveEngines();
}

bool SearchEngineManager::removeEngine(const QString& id)
{
    for (int i = 0; i < m_engines.size(); ++i) {
        if (m_engines[i].id == id && !m_engines[i].builtin) {
            m_engines.removeAt(i);
            saveEngines();
            return true;
        }
    }
    return false;
}

bool SearchEngineManager::setDefault(const QString& id)
{
    bool found = false;
    for (auto& e : m_engines) {
        if (e.id == id) {
            e.isDefault = true;
            m_defaultId = id;
            found = true;
        } else {
            e.isDefault = false;
        }
    }
    if (found) {
        saveEngines();
        emit defaultChanged(id);
    }
    return found;
}

QUrl SearchEngineManager::search(const QString& query) const
{
    // 단축키 체크
    for (const auto& e : m_engines) {
        if (!e.shortcut.isEmpty() && query.startsWith(e.shortcut + " ")) {
            QString q = query.mid(e.shortcut.length() + 1);
            return e.buildSearchUrl(q);
        }
    }

    // 기본 엔진
    for (const auto& e : m_engines) {
        if (e.isDefault) return e.buildSearchUrl(query);
    }
    // fallback
    return QUrl("https://duckduckgo.com/?q=" + QUrl::toPercentEncoding(query));
}

QUrl SearchEngineManager::searchWith(const QString& engineId, const QString& query) const
{
    for (const auto& e : m_engines) {
        if (e.id == engineId) return e.buildSearchUrl(query);
    }
    return search(query);
}

QPair<QString, QString> SearchEngineManager::parseShortcut(const QString& input) const
{
    for (const auto& e : m_engines) {
        if (!e.shortcut.isEmpty() && input.startsWith(e.shortcut + " ")) {
            return {e.id, input.mid(e.shortcut.length() + 1)};
        }
    }
    return {"", input};
}

SearchEngineEntry* SearchEngineManager::defaultEngine()
{
    for (auto& e : m_engines) {
        if (e.isDefault) return &e;
    }
    return m_engines.isEmpty() ? nullptr : &m_engines[0];
}

SearchEngineEntry* SearchEngineManager::findById(const QString& id)
{
    for (auto& e : m_engines) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

SearchEngineEntry* SearchEngineManager::findByShortcut(const QString& shortcut)
{
    for (auto& e : m_engines) {
        if (e.shortcut == shortcut) return &e;
    }
    return nullptr;
}

void SearchEngineManager::saveEngines()
{
    QJsonArray arr;
    for (const auto& e : m_engines) arr.append(e.toJson());

    QFile file(m_storagePath + "/search_engines.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }
}

void SearchEngineManager::loadEngines()
{
    QFile file(m_storagePath + "/search_engines.json");
    if (!file.open(QIODevice::ReadOnly)) return;

    auto doc = QJsonDocument::fromJson(file.readAll());
    auto arr = doc.array();
    m_engines.clear();

    for (const auto& val : arr) {
        auto obj = val.toObject();
        SearchEngineEntry e;
        e.id = obj["id"].toString();
        e.name = obj["name"].toString();
        e.searchUrl = QUrl(obj["searchUrl"].toString());
        e.suggestUrl = QUrl(obj["suggestUrl"].toString());
        e.iconUrl = QUrl(obj["iconUrl"].toString());
        e.shortcut = obj["shortcut"].toString();
        e.isDefault = obj["isDefault"].toBool();
        e.builtin = obj["builtin"].toBool();
        m_engines.append(e);

        if (e.isDefault) m_defaultId = e.id;
    }
}

// ============================================================
// SyncProtocol
// ============================================================

SyncProtocol::SyncProtocol(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_storagePath(storagePath)
{
}

bool SyncProtocol::exportBookmarks(const QString& path) const
{
    QFile src(m_storagePath + "/bookmarks.db");
    return src.copy(path);
}

bool SyncProtocol::exportHistory(const QString& path) const
{
    QFile src(m_storagePath + "/history.db");
    return src.copy(path);
}

bool SyncProtocol::exportPasswords(const QString& path) const
{
    QFile src(m_storagePath + "/credentials.db");
    return src.copy(path);
}

bool SyncProtocol::exportSettings(const QString& path) const
{
    QFile src(m_storagePath + "/search_engines.json");
    return src.copy(path);
}

bool SyncProtocol::exportAll(const QString& dirPath) const
{
    QDir().mkpath(dirPath);
    bool ok = true;
    ok &= exportBookmarks(dirPath + "/bookmarks.db");
    ok &= exportHistory(dirPath + "/history.db");
    ok &= exportPasswords(dirPath + "/credentials.db");
    ok &= exportSettings(dirPath + "/search_engines.json");
    return ok;
}

bool SyncProtocol::importBookmarks(const QString& path)
{
    QString dest = m_storagePath + "/bookmarks.db";
    QFile::remove(dest);
    return QFile::copy(path, dest);
}

bool SyncProtocol::importHistory(const QString& path)
{
    QString dest = m_storagePath + "/history.db";
    QFile::remove(dest);
    return QFile::copy(path, dest);
}

bool SyncProtocol::importSettings(const QString& path)
{
    QString dest = m_storagePath + "/search_engines.json";
    QFile::remove(dest);
    return QFile::copy(path, dest);
}

bool SyncProtocol::importAll(const QString& dirPath)
{
    bool ok = true;
    ok &= importBookmarks(dirPath + "/bookmarks.db");
    ok &= importHistory(dirPath + "/history.db");
    ok &= importSettings(dirPath + "/search_engines.json");
    return ok;
}

} // namespace Engine
} // namespace Ordinal
