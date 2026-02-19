#include "permissions_access.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QSet>
#include <iostream>

namespace Ordinal {
namespace Engine {

// ============================================================
// NotificationManager
// ============================================================

NotificationManager::NotificationManager(QObject* parent)
    : QObject(parent)
{
}

void NotificationManager::addNotification(const QUrl& origin, const QString& title, const QString& body)
{
    WebNotification notif;
    notif.id = m_nextId++;
    notif.origin = origin;
    notif.title = title;
    notif.body = body;
    notif.timestamp = QDateTime::currentDateTime();
    m_notifications.prepend(notif);

    // 최대 500개 유지
    while (m_notifications.size() > 500) m_notifications.removeLast();

    emit notificationReceived(notif);
}

QList<WebNotification> NotificationManager::recent(int limit) const
{
    return m_notifications.mid(0, qMin(limit, m_notifications.size()));
}

void NotificationManager::markAsRead(int64_t id)
{
    for (auto& n : m_notifications) {
        if (n.id == id) { n.read = true; break; }
    }
}

void NotificationManager::clearAll()
{
    m_notifications.clear();
}

int NotificationManager::unreadCount() const
{
    int count = 0;
    for (const auto& n : m_notifications) {
        if (!n.read) count++;
    }
    return count;
}

// ============================================================
// PermissionManager
// ============================================================

PermissionManager::PermissionManager(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_dbPath(storagePath + "/permissions.db")
{
    QDir().mkpath(storagePath);
    initDatabase();

    // 기본 정책: 모두 Ask
    for (int i = 0; i <= static_cast<int>(SitePermission::FileSystem); ++i) {
        m_defaults[static_cast<SitePermission::Permission>(i)] = SitePermission::Ask;
    }
    // 팝업은 기본 차단
    m_defaults[SitePermission::Popups] = SitePermission::Deny;
}

PermissionManager::~PermissionManager()
{
    if (m_db.isOpen()) m_db.close();
}

void PermissionManager::initDatabase()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "permissions");
    m_db.setDatabaseName(m_dbPath);
    if (!m_db.open()) return;

    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS permissions (
            origin TEXT NOT NULL,
            permission INTEGER NOT NULL,
            state INTEGER NOT NULL DEFAULT 0,
            modified TEXT NOT NULL DEFAULT (datetime('now')),
            PRIMARY KEY(origin, permission)
        )
    )");
}

void PermissionManager::setPermission(const QUrl& origin, SitePermission::Permission perm,
                                       SitePermission::State state)
{
    QString host = origin.host();
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO permissions (origin, permission, state, modified) "
              "VALUES (?, ?, ?, datetime('now'))");
    q.addBindValue(host);
    q.addBindValue(static_cast<int>(perm));
    q.addBindValue(static_cast<int>(state));
    q.exec();

    emit permissionChanged(origin, perm, state);
}

SitePermission::State PermissionManager::getPermission(const QUrl& origin,
                                                         SitePermission::Permission perm) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT state FROM permissions WHERE origin = ? AND permission = ?");
    q.addBindValue(origin.host());
    q.addBindValue(static_cast<int>(perm));
    q.exec();
    if (q.next()) {
        return static_cast<SitePermission::State>(q.value(0).toInt());
    }
    return m_defaults.value(perm, SitePermission::Ask);
}

void PermissionManager::removePermission(const QUrl& origin, SitePermission::Permission perm)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM permissions WHERE origin = ? AND permission = ?");
    q.addBindValue(origin.host());
    q.addBindValue(static_cast<int>(perm));
    q.exec();
}

void PermissionManager::removeAllPermissions(const QUrl& origin)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM permissions WHERE origin = ?");
    q.addBindValue(origin.host());
    q.exec();
}

QList<SitePermission> PermissionManager::permissionsForSite(const QUrl& origin) const
{
    QList<SitePermission> perms;
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM permissions WHERE origin = ?");
    q.addBindValue(origin.host());
    q.exec();
    while (q.next()) {
        SitePermission sp;
        sp.origin = origin;
        sp.permission = static_cast<SitePermission::Permission>(q.value("permission").toInt());
        sp.state = static_cast<SitePermission::State>(q.value("state").toInt());
        sp.lastModified = QDateTime::fromString(q.value("modified").toString(), Qt::ISODate);
        perms.append(sp);
    }
    return perms;
}

QList<SitePermission> PermissionManager::allPermissions() const
{
    QList<SitePermission> perms;
    QSqlQuery q(m_db);
    q.exec("SELECT * FROM permissions ORDER BY origin, permission");
    while (q.next()) {
        SitePermission sp;
        sp.origin = QUrl("https://" + q.value("origin").toString());
        sp.permission = static_cast<SitePermission::Permission>(q.value("permission").toInt());
        sp.state = static_cast<SitePermission::State>(q.value("state").toInt());
        sp.lastModified = QDateTime::fromString(q.value("modified").toString(), Qt::ISODate);
        perms.append(sp);
    }
    return perms;
}

QMap<QString, int> PermissionManager::permissionStats() const
{
    QMap<QString, int> stats;
    QSqlQuery q(m_db);
    q.exec("SELECT permission, COUNT(*) FROM permissions GROUP BY permission");
    while (q.next()) {
        int perm = q.value(0).toInt();
        stats[permissionName(static_cast<SitePermission::Permission>(perm))] = q.value(1).toInt();
    }
    return stats;
}

void PermissionManager::setDefaultPolicy(SitePermission::Permission perm,
                                          SitePermission::State state)
{
    m_defaults[perm] = state;
}

SitePermission::State PermissionManager::defaultPolicy(SitePermission::Permission perm) const
{
    return m_defaults.value(perm, SitePermission::Ask);
}

QString PermissionManager::permissionName(SitePermission::Permission perm)
{
    switch (perm) {
    case SitePermission::Notifications: return "알림";
    case SitePermission::Camera: return "카메라";
    case SitePermission::Microphone: return "마이크";
    case SitePermission::Location: return "위치";
    case SitePermission::Clipboard: return "클립보드";
    case SitePermission::Popups: return "팝업";
    case SitePermission::AutoPlay: return "자동재생";
    case SitePermission::Sensors: return "센서";
    case SitePermission::Midi: return "MIDI";
    case SitePermission::FullScreen: return "전체화면";
    case SitePermission::FileSystem: return "파일시스템";
    }
    return "알 수 없음";
}

// ============================================================
// AccessibilityTools
// ============================================================

AccessibilityTools::AccessibilityTools(QObject* parent)
    : QObject(parent)
{
}

void AccessibilityTools::setTextScale(double scale)
{
    m_textScale = qBound(0.5, scale, 3.0);
    emit settingsChanged();
}

void AccessibilityTools::setHighContrast(bool enabled)
{
    m_highContrast = enabled;
    emit settingsChanged();
}

void AccessibilityTools::setDyslexiaFont(bool enabled)
{
    m_dyslexiaFont = enabled;
    emit settingsChanged();
}

void AccessibilityTools::setFocusHighlight(bool enabled)
{
    m_focusHighlight = enabled;
    emit settingsChanged();
}

void AccessibilityTools::setReduceMotion(bool enabled)
{
    m_reduceMotion = enabled;
    emit settingsChanged();
}

void AccessibilityTools::setLargeCursor(bool enabled)
{
    m_largeCursor = enabled;
    emit settingsChanged();
}

QString AccessibilityTools::highContrastCss()
{
    return R"(
        * { color: #fff !important; background: #000 !important; border-color: #fff !important; }
        a { color: #ff0 !important; text-decoration: underline !important; }
        img { filter: contrast(1.5) brightness(1.2) !important; }
        input, textarea, select { background: #333 !important; color: #fff !important;
            border: 2px solid #fff !important; }
    )";
}

QString AccessibilityTools::dyslexiaFontCss()
{
    return R"(
        @import url('https://fonts.googleapis.com/css2?family=Lexend:wght@300;400;500;700&display=swap');
        * { font-family: 'Lexend', 'OpenDyslexic', sans-serif !important; }
        p, li, td, th, span, div { letter-spacing: 0.05em !important;
            word-spacing: 0.1em !important; line-height: 1.8 !important; }
    )";
}

QString AccessibilityTools::focusHighlightCss()
{
    return R"(
        *:focus { outline: 3px solid #ff6600 !important; outline-offset: 2px !important; }
        a:focus, button:focus, input:focus, select:focus, textarea:focus {
            box-shadow: 0 0 0 4px rgba(255,102,0,0.5) !important; }
    )";
}

QString AccessibilityTools::reduceMotionCss()
{
    return R"(
        *, *::before, *::after {
            animation-duration: 0.001ms !important;
            animation-iteration-count: 1 !important;
            transition-duration: 0.001ms !important;
            scroll-behavior: auto !important;
        }
    )";
}

QString AccessibilityTools::largeCursorCss()
{
    return R"(
        * { cursor: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='32' height='32'%3E%3Cpath d='M5 2l20 10-8 3-3 8z' fill='black' stroke='white' stroke-width='1'/%3E%3C/svg%3E") 5 2, auto !important; }
    )";
}

QString AccessibilityTools::generateInjectionCss() const
{
    QString css;
    if (m_highContrast) css += highContrastCss();
    if (m_dyslexiaFont) css += dyslexiaFontCss();
    if (m_focusHighlight) css += focusHighlightCss();
    if (m_reduceMotion) css += reduceMotionCss();
    if (m_largeCursor) css += largeCursorCss();

    if (m_textScale != 1.0) {
        css += QString("html { font-size: %1%% !important; }").arg(m_textScale * 100);
    }

    return css;
}

QString AccessibilityTools::extractReadableText()
{
    return R"(
(function() {
    var walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT, null, false);
    var text = '';
    var node;
    while (node = walker.nextNode()) {
        var parent = node.parentElement;
        if (parent && !['SCRIPT','STYLE','NOSCRIPT'].includes(parent.tagName)) {
            var t = node.textContent.trim();
            if (t.length > 0) text += t + ' ';
        }
    }
    return text.substring(0, 10000);
})();
    )";
}

// ============================================================
// TabSleepManager
// ============================================================

TabSleepManager::TabSleepManager(QObject* parent)
    : QObject(parent)
    , m_checkTimer(new QTimer(this))
{
    m_checkTimer->setInterval(60000); // 1분마다 체크
    connect(m_checkTimer, &QTimer::timeout, this, &TabSleepManager::checkSleepCandidates);
    m_checkTimer->start();
}

void TabSleepManager::setSleepTimeout(int minutes)
{
    m_sleepMinutes = qMax(1, minutes);
}

void TabSleepManager::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (enabled) m_checkTimer->start();
    else m_checkTimer->stop();
}

void TabSleepManager::onTabActivated(int tabIndex)
{
    m_lastActive[tabIndex] = QDateTime::currentDateTime();

    // 수면 중이면 깨우기
    if (m_sleeping.contains(tabIndex)) {
        wakeTab(tabIndex);
    }
}

void TabSleepManager::onTabCreated(int tabIndex)
{
    m_lastActive[tabIndex] = QDateTime::currentDateTime();
}

void TabSleepManager::onTabRemoved(int tabIndex)
{
    m_lastActive.remove(tabIndex);
    m_sleeping.remove(tabIndex);
    m_pinned.remove(tabIndex);
}

bool TabSleepManager::shouldSleep(int tabIndex) const
{
    if (!m_enabled) return false;
    if (m_pinned.contains(tabIndex)) return false;
    if (m_sleeping.contains(tabIndex)) return false;

    auto it = m_lastActive.find(tabIndex);
    if (it == m_lastActive.end()) return false;

    qint64 inactiveMs = it.value().msecsTo(QDateTime::currentDateTime());
    return inactiveMs > (m_sleepMinutes * 60 * 1000);
}

QList<int> TabSleepManager::sleepCandidates() const
{
    QList<int> candidates;
    for (auto it = m_lastActive.begin(); it != m_lastActive.end(); ++it) {
        if (shouldSleep(it.key())) {
            candidates.append(it.key());
        }
    }
    return candidates;
}

void TabSleepManager::sleepTab(int tabIndex)
{
    if (m_sleeping.contains(tabIndex)) return;
    m_sleeping.insert(tabIndex);
    emit tabSlept(tabIndex);
}

void TabSleepManager::wakeTab(int tabIndex)
{
    if (!m_sleeping.contains(tabIndex)) return;
    m_sleeping.remove(tabIndex);
    m_lastActive[tabIndex] = QDateTime::currentDateTime();
    emit tabWoken(tabIndex);
}

bool TabSleepManager::isSleeping(int tabIndex) const
{
    return m_sleeping.contains(tabIndex);
}

int64_t TabSleepManager::estimatedMemorySaved() const
{
    // 탭당 약 100MB 추정
    return m_sleeping.size() * 100LL * 1024 * 1024;
}

void TabSleepManager::checkSleepCandidates()
{
    if (!m_enabled) return;

    auto candidates = sleepCandidates();
    for (int idx : candidates) {
        sleepTab(idx);
    }
}

} // namespace Engine
} // namespace Ordinal
