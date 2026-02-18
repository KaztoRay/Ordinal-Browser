#include "newtab_credentials.h"
#include "data_manager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QDir>
#include <QPixmap>
#include <QScreen>
#include <QApplication>
#include <QStandardPaths>
#include <QStyleHints>
#include <QDateTime>
#include <QPageLayout>
#include <QPageSize>
#include <QTimer>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QWebEngineView>
#include <iostream>

namespace Ordinal {
namespace Engine {

// ============================================================
// SpeedDialItem
// ============================================================

QJsonObject SpeedDialItem::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["title"] = title;
    obj["url"] = url.toString();
    obj["position"] = position;
    obj["visitCount"] = visitCount;
    obj["pinned"] = pinned;
    return obj;
}

// ============================================================
// NewTabPageGenerator
// ============================================================

NewTabPageGenerator::NewTabPageGenerator(HistoryManager* history, QObject* parent)
    : QObject(parent)
    , m_history(history)
{
}

QString NewTabPageGenerator::generateHtml() const
{
    bool darkMode = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto scheme = QApplication::styleHints()->colorScheme();
    darkMode = (scheme == Qt::ColorScheme::Dark);
#endif

    return QString(R"(
<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="utf-8">
<title>새 탭 — Ordinal Browser</title>
<style>%1</style>
</head>
<body>
<div class="container">
    <div class="logo">
        <h1>🔒 Ordinal</h1>
        <p class="subtitle">AI 기반 보안 브라우저</p>
    </div>
    %2
    <div class="speed-dial">
        <h3>자주 방문</h3>
        <div class="grid">
            %3
        </div>
    </div>
    <div class="footer">
        <span>광고 차단 활성화</span> · 
        <span>WebRTC 보호</span> · 
        <span>핑거프린팅 방지</span>
    </div>
</div>
</body>
</html>
)").arg(generateStylesheet(darkMode),
       generateSearchBarHtml(),
       generateSpeedDialHtml());
}

QString NewTabPageGenerator::generateSearchBarHtml() const
{
    return R"(
    <div class="search-box">
        <form action="https://duckduckgo.com/" method="get">
            <input type="text" name="q" placeholder="DuckDuckGo로 검색..." autofocus>
        </form>
    </div>
    )";
}

QString NewTabPageGenerator::generateSpeedDialHtml() const
{
    auto sites = getMostVisitedSites(8);
    QString html;

    for (const auto& site : sites) {
        QString initial = site.title.isEmpty() ? "?" : site.title.left(1).toUpper();
        QString domain = site.url.host();

        html += QString(R"(
        <a class="dial-item" href="%1" title="%2">
            <div class="dial-icon">
                <img src="%3" onerror="this.style.display='none'; this.nextSibling.style.display='flex';" alt="">
                <span class="dial-letter" style="display:none">%4</span>
            </div>
            <span class="dial-title">%5</span>
        </a>
        )").arg(site.url.toString(),
               site.title,
               faviconUrl(site.url),
               initial,
               domain.left(20));
    }

    // 빈 슬롯 채우기
    int empty = 8 - sites.size();
    for (int i = 0; i < empty; ++i) {
        html += R"(
        <div class="dial-item empty">
            <div class="dial-icon"><span class="dial-letter">+</span></div>
            <span class="dial-title"></span>
        </div>
        )";
    }

    return html;
}

QList<SpeedDialItem> NewTabPageGenerator::getMostVisitedSites(int limit) const
{
    QList<SpeedDialItem> items;

    // 고정 항목 먼저
    for (const auto& pinned : m_pinnedItems) {
        items.append(pinned);
    }

    // 히스토리에서 자주 방문
    if (m_history) {
        auto most = m_history->getMostVisited(limit);
        for (const auto& entry : most) {
            if (items.size() >= limit) break;

            // 중복 체크
            bool exists = false;
            for (const auto& item : items) {
                if (item.url.host() == entry.url.host()) { exists = true; break; }
            }
            if (exists) continue;

            SpeedDialItem item;
            item.title = entry.title;
            item.url = entry.url;
            item.visitCount = entry.visitCount;
            items.append(item);
        }
    }

    return items;
}

void NewTabPageGenerator::addSpeedDial(const QString& title, const QUrl& url)
{
    SpeedDialItem item;
    item.title = title;
    item.url = url;
    item.pinned = true;
    item.position = m_pinnedItems.size();
    m_pinnedItems.append(item);
}

void NewTabPageGenerator::removeSpeedDial(int64_t id)
{
    m_pinnedItems.removeIf([id](const SpeedDialItem& item) { return item.id == id; });
}

void NewTabPageGenerator::pinSpeedDial(int64_t id, bool pinned)
{
    for (auto& item : m_pinnedItems) {
        if (item.id == id) { item.pinned = pinned; break; }
    }
}

QList<SpeedDialItem> NewTabPageGenerator::getSpeedDialItems() const
{
    return m_pinnedItems;
}

QString NewTabPageGenerator::faviconUrl(const QUrl& siteUrl) const
{
    return "https://www.google.com/s2/favicons?domain=" + siteUrl.host() + "&sz=64";
}

QString NewTabPageGenerator::generateStylesheet(bool darkMode) const
{
    QString bg = darkMode ? "#1a1a1a" : "#f5f5f5";
    QString cardBg = darkMode ? "#2d2d2d" : "white";
    QString text = darkMode ? "#e0e0e0" : "#333";
    QString textSub = darkMode ? "#888" : "#999";
    QString border = darkMode ? "#3d3d3d" : "#e0e0e0";
    QString inputBg = darkMode ? "#252525" : "white";
    QString shadow = darkMode ? "rgba(0,0,0,0.4)" : "rgba(0,0,0,0.1)";

    return QString(R"(
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            background: %1;
            color: %2;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            min-height: 100vh;
        }
        .container {
            max-width: 700px;
            margin: 0 auto;
            padding: 80px 20px 40px;
            text-align: center;
        }
        .logo h1 {
            font-size: 36px;
            font-weight: 300;
            margin-bottom: 4px;
        }
        .subtitle {
            color: %3;
            font-size: 14px;
            margin-bottom: 32px;
        }
        .search-box {
            margin: 24px auto 40px;
            max-width: 560px;
        }
        .search-box input {
            width: 100%%;
            padding: 12px 20px;
            font-size: 16px;
            border: 1px solid %4;
            border-radius: 24px;
            background: %5;
            color: %2;
            outline: none;
            box-shadow: 0 2px 6px %6;
            transition: all 0.2s;
        }
        .search-box input:focus {
            border-color: #4285f4;
            box-shadow: 0 2px 12px rgba(66,133,244,0.3);
        }
        .speed-dial h3 {
            font-size: 13px;
            font-weight: 500;
            color: %3;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            margin-bottom: 16px;
        }
        .grid {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 16px;
            max-width: 560px;
            margin: 0 auto;
        }
        .dial-item {
            display: flex;
            flex-direction: column;
            align-items: center;
            text-decoration: none;
            color: %2;
            padding: 12px 8px;
            border-radius: 12px;
            transition: background 0.15s;
        }
        .dial-item:hover {
            background: %7;
        }
        .dial-item.empty {
            opacity: 0.3;
        }
        .dial-icon {
            width: 52px; height: 52px;
            border-radius: 12px;
            background: %7;
            display: flex;
            align-items: center;
            justify-content: center;
            margin-bottom: 8px;
            overflow: hidden;
        }
        .dial-icon img {
            width: 32px; height: 32px;
        }
        .dial-letter {
            font-size: 22px;
            font-weight: 600;
            color: #4285f4;
        }
        .dial-title {
            font-size: 12px;
            color: %3;
            max-width: 100px;
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }
        .footer {
            margin-top: 60px;
            font-size: 12px;
            color: %3;
        }
        .footer span {
            color: #34a853;
        }
    )").arg(bg, text, textSub, border, inputBg, shadow, cardBg);
}

// ============================================================
// PasswordEntry
// ============================================================

QJsonObject PasswordEntry::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["siteUrl"] = siteUrl.toString();
    obj["username"] = username;
    obj["created"] = created.toString(Qt::ISODate);
    obj["lastUsed"] = lastUsed.toString(Qt::ISODate);
    return obj;
}

// ============================================================
// CredentialManager
// ============================================================

CredentialManager::CredentialManager(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_dbPath(storagePath + "/credentials.db")
{
    QDir().mkpath(storagePath);
    initDatabase();
}

CredentialManager::~CredentialManager()
{
    if (m_db.isOpen()) m_db.close();
}

void CredentialManager::initDatabase()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "credentials");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        std::cerr << "[Credentials] DB 열기 실패: "
                  << m_db.lastError().text().toStdString() << std::endl;
        return;
    }

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS credentials (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            site_url TEXT NOT NULL,
            username TEXT NOT NULL,
            password TEXT NOT NULL,
            created TEXT NOT NULL DEFAULT (datetime('now')),
            last_used TEXT NOT NULL DEFAULT (datetime('now')),
            auto_fill INTEGER NOT NULL DEFAULT 1,
            UNIQUE(site_url, username)
        )
    )");

    q.exec(R"(
        CREATE TABLE IF NOT EXISTS master_key (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            salt TEXT NOT NULL,
            hash TEXT NOT NULL
        )
    )");

    q.exec("CREATE INDEX IF NOT EXISTS idx_cred_url ON credentials(site_url)");

    // salt 확인
    q.exec("SELECT salt FROM master_key WHERE id = 1");
    if (q.next()) {
        m_salt = q.value(0).toString();
    } else {
        // 새 salt 생성
        QByteArray saltBytes(32, 0);
        QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(saltBytes.data()),
                                               saltBytes.size() / sizeof(quint32));
        m_salt = saltBytes.toHex();
    }
}

int64_t CredentialManager::saveCredential(const QUrl& siteUrl, const QString& username,
                                           const QString& password)
{
    QString encrypted = encrypt(password);

    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO credentials (site_url, username, password, created, last_used) "
              "VALUES (?, ?, ?, datetime('now'), datetime('now'))");
    q.addBindValue(siteUrl.toString());
    q.addBindValue(username);
    q.addBindValue(encrypted);

    if (!q.exec()) {
        std::cerr << "[Credentials] 저장 실패: "
                  << q.lastError().text().toStdString() << std::endl;
        return -1;
    }

    int64_t id = q.lastInsertId().toLongLong();
    PasswordEntry entry;
    entry.id = id;
    entry.siteUrl = siteUrl;
    entry.username = username;
    entry.created = QDateTime::currentDateTime();
    emit credentialSaved(entry);
    return id;
}

bool CredentialManager::removeCredential(int64_t id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM credentials WHERE id = ?");
    q.addBindValue(id);
    if (q.exec()) {
        emit credentialRemoved(id);
        return true;
    }
    return false;
}

bool CredentialManager::removeByUrl(const QUrl& siteUrl)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM credentials WHERE site_url = ?");
    q.addBindValue(siteUrl.toString());
    return q.exec();
}

bool CredentialManager::updatePassword(int64_t id, const QString& newPassword)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE credentials SET password = ?, last_used = datetime('now') WHERE id = ?");
    q.addBindValue(encrypt(newPassword));
    q.addBindValue(id);
    return q.exec();
}

QList<PasswordEntry> CredentialManager::getCredentials(const QUrl& siteUrl) const
{
    QList<PasswordEntry> entries;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM credentials WHERE site_url = ? ORDER BY last_used DESC");
    q.addBindValue(siteUrl.toString());
    q.exec();

    while (q.next()) {
        PasswordEntry entry;
        entry.id = q.value("id").toLongLong();
        entry.siteUrl = QUrl(q.value("site_url").toString());
        entry.username = q.value("username").toString();
        entry.encryptedPassword = q.value("password").toString();
        entry.created = QDateTime::fromString(q.value("created").toString(), Qt::ISODate);
        entry.lastUsed = QDateTime::fromString(q.value("last_used").toString(), Qt::ISODate);
        entry.autoFill = q.value("auto_fill").toBool();
        entries.append(entry);
    }
    return entries;
}

QList<PasswordEntry> CredentialManager::getAllCredentials() const
{
    QList<PasswordEntry> entries;
    QSqlQuery q(m_db);
    q.exec("SELECT * FROM credentials ORDER BY site_url, username");

    while (q.next()) {
        PasswordEntry entry;
        entry.id = q.value("id").toLongLong();
        entry.siteUrl = QUrl(q.value("site_url").toString());
        entry.username = q.value("username").toString();
        entry.encryptedPassword = q.value("password").toString();
        entry.created = QDateTime::fromString(q.value("created").toString(), Qt::ISODate);
        entry.lastUsed = QDateTime::fromString(q.value("last_used").toString(), Qt::ISODate);
        entry.autoFill = q.value("auto_fill").toBool();
        entries.append(entry);
    }
    return entries;
}

std::optional<PasswordEntry> CredentialManager::findCredential(const QUrl& siteUrl,
                                                                 const QString& username) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM credentials WHERE site_url = ? AND username = ?");
    q.addBindValue(siteUrl.toString());
    q.addBindValue(username);
    q.exec();

    if (q.next()) {
        PasswordEntry entry;
        entry.id = q.value("id").toLongLong();
        entry.siteUrl = QUrl(q.value("site_url").toString());
        entry.username = q.value("username").toString();
        entry.encryptedPassword = q.value("password").toString();
        return entry;
    }
    return std::nullopt;
}

bool CredentialManager::hasCredential(const QUrl& siteUrl) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM credentials WHERE site_url = ?");
    q.addBindValue(siteUrl.toString());
    q.exec();
    q.next();
    return q.value(0).toInt() > 0;
}

bool CredentialManager::setMasterPassword(const QString& password)
{
    QString hash = deriveKey(password);

    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO master_key (id, salt, hash) VALUES (1, ?, ?)");
    q.addBindValue(m_salt);
    q.addBindValue(hash);

    if (q.exec()) {
        m_derivedKey = hash;
        m_locked = false;
        emit lockedStateChanged(false);
        return true;
    }
    return false;
}

bool CredentialManager::verifyMasterPassword(const QString& password) const
{
    QSqlQuery q(m_db);
    q.exec("SELECT hash FROM master_key WHERE id = 1");
    if (!q.next()) return false;

    QString storedHash = q.value(0).toString();
    return deriveKey(password) == storedHash;
}

void CredentialManager::lock()
{
    m_locked = true;
    m_derivedKey.clear();
    emit lockedStateChanged(true);
}

bool CredentialManager::unlock(const QString& masterPassword)
{
    if (verifyMasterPassword(masterPassword)) {
        m_locked = false;
        m_derivedKey = deriveKey(masterPassword);
        emit lockedStateChanged(false);
        return true;
    }
    return false;
}

QString CredentialManager::encrypt(const QString& plaintext) const
{
    // XOR 기반 간이 암호화 (실제 프로덕션에서는 AES-256 사용)
    QByteArray data = plaintext.toUtf8();
    QByteArray key = m_derivedKey.isEmpty() ?
        QByteArray("ordinal-default-key-2026") : m_derivedKey.toUtf8();

    QByteArray result;
    result.reserve(data.size());
    for (int i = 0; i < data.size(); ++i) {
        result.append(data[i] ^ key[i % key.size()]);
    }
    return result.toBase64();
}

QString CredentialManager::decrypt(const QString& ciphertext) const
{
    QByteArray data = QByteArray::fromBase64(ciphertext.toUtf8());
    QByteArray key = m_derivedKey.isEmpty() ?
        QByteArray("ordinal-default-key-2026") : m_derivedKey.toUtf8();

    QByteArray result;
    result.reserve(data.size());
    for (int i = 0; i < data.size(); ++i) {
        result.append(data[i] ^ key[i % key.size()]);
    }
    return QString::fromUtf8(result);
}

QString CredentialManager::generatePassword(int length, bool symbols,
                                             bool numbers, bool uppercase)
{
    QString chars = "abcdefghijklmnopqrstuvwxyz";
    if (uppercase) chars += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (numbers) chars += "0123456789";
    if (symbols) chars += "!@#$%^&*()-_=+[]{}|;:,.<>?";

    QString result;
    result.reserve(length);
    for (int i = 0; i < length; ++i) {
        int idx = QRandomGenerator::global()->bounded(chars.length());
        result.append(chars[idx]);
    }
    return result;
}

int CredentialManager::totalCount() const
{
    QSqlQuery q(m_db);
    q.exec("SELECT COUNT(*) FROM credentials");
    q.next();
    return q.value(0).toInt();
}

QString CredentialManager::deriveKey(const QString& password) const
{
    // PBKDF2-like: SHA-256 반복 해싱
    QByteArray data = (password + m_salt).toUtf8();
    for (int i = 0; i < 10000; ++i) {
        data = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    }
    return data.toHex();
}

// ============================================================
// ScreenCapture
// ============================================================

ScreenCapture::ScreenCapture(QObject* parent)
    : QObject(parent)
{
}

void ScreenCapture::captureVisibleArea(QWidget* webView, const QString& savePath)
{
    if (!webView) {
        emit captureError("웹뷰가 없습니다");
        return;
    }

    QPixmap pixmap = webView->grab();
    QString path = savePath.isEmpty() ? defaultSavePath("screenshot", "png") : savePath;

    if (pixmap.save(path)) {
        emit captureCompleted(path);
    } else {
        emit captureError("스크린샷 저장 실패: " + path);
    }
}

void ScreenCapture::captureFullPage(QWidget* webView, const QString& savePath)
{
    // 전체 페이지 캡처는 QWebEngineView의 printToPdf 사용
    auto* view = qobject_cast<QWebEngineView*>(webView);
    if (!view) {
        captureVisibleArea(webView, savePath);
        return;
    }

    // 현재는 보이는 영역만 캡처 (전체 페이지는 스크롤 필요)
    captureVisibleArea(webView, savePath);
}

void ScreenCapture::printToPdf(QWidget* webView, const QString& savePath)
{
    auto* view = qobject_cast<QWebEngineView*>(webView);
    if (!view) {
        emit captureError("PDF 변환 불가");
        return;
    }

    QString path = savePath.isEmpty() ? defaultSavePath("page", "pdf") : savePath;

    view->page()->printToPdf(path, QPageLayout(
        QPageSize(QPageSize::A4),
        QPageLayout::Portrait,
        QMarginsF(10, 10, 10, 10),
        QPageLayout::Millimeter));

    // printToPdf는 비동기지만 파일 완성까지 약간의 딜레이
    QTimer::singleShot(2000, this, [this, path]() {
        if (QFile::exists(path)) {
            emit captureCompleted(path);
        }
    });
}

QString ScreenCapture::defaultSavePath(const QString& prefix, const QString& ext)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    return dir + "/" + prefix + "_" + timestamp + "." + ext;
}

} // namespace Engine
} // namespace Ordinal
