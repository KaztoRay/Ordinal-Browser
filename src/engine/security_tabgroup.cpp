#include "security_tabgroup.h"

#include <QRegularExpression>
#include <QDateTime>
#include <QDir>
#include <QWebEngineSettings>
#include <iostream>

namespace Ordinal {
namespace Engine {

// ============================================================
// SecurityAlert
// ============================================================

QJsonObject SecurityAlert::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["severity"] = severityString();
    obj["type"] = static_cast<int>(type);
    obj["url"] = url.toString();
    obj["title"] = title;
    obj["description"] = description;
    obj["recommendation"] = recommendation;
    obj["timestamp"] = timestamp.toString(Qt::ISODate);
    obj["dismissed"] = dismissed;
    return obj;
}

QString SecurityAlert::severityString() const
{
    switch (severity) {
    case Info: return "info";
    case Low: return "low";
    case Medium: return "medium";
    case High: return "high";
    case Critical: return "critical";
    }
    return "unknown";
}

QColor SecurityAlert::severityColor() const
{
    switch (severity) {
    case Info: return QColor("#4285f4");
    case Low: return QColor("#34a853");
    case Medium: return QColor("#fbbc05");
    case High: return QColor("#ea4335");
    case Critical: return QColor("#b31412");
    }
    return QColor("#888");
}

// ============================================================
// SecurityScanner
// ============================================================

SecurityScanner::SecurityScanner(QObject* parent)
    : QObject(parent)
{
}

QList<SecurityAlert> SecurityScanner::scanUrl(const QUrl& url) const
{
    QList<SecurityAlert> alerts;

    // HTTP (비암호화) 경고
    if (url.scheme() == "http") {
        SecurityAlert alert;
        alert.severity = SecurityAlert::Medium;
        alert.type = SecurityAlert::Privacy;
        alert.url = url;
        alert.title = "비암호화 연결";
        alert.description = "이 사이트는 HTTPS를 사용하지 않습니다. 데이터가 암호화되지 않습니다.";
        alert.recommendation = "개인정보를 입력하지 마세요.";
        alert.timestamp = QDateTime::currentDateTime();
        alerts.append(alert);
    }

    // 피싱 검사
    if (isPhishing(url)) {
        SecurityAlert alert;
        alert.severity = SecurityAlert::Critical;
        alert.type = SecurityAlert::Phishing;
        alert.url = url;
        alert.title = "피싱 의심 사이트";
        alert.description = "이 URL은 피싱 사이트 패턴과 일치합니다.";
        alert.recommendation = "즉시 페이지를 닫으세요.";
        alert.timestamp = QDateTime::currentDateTime();
        alerts.append(alert);
    }

    return alerts;
}

QList<SecurityAlert> SecurityScanner::scanPageContent(const QString& html, const QUrl& pageUrl) const
{
    QList<SecurityAlert> alerts;

    // 크립토마이너
    if (hasCryptominer(html)) {
        SecurityAlert alert;
        alert.severity = SecurityAlert::High;
        alert.type = SecurityAlert::Cryptominer;
        alert.url = pageUrl;
        alert.title = "크립토마이너 탐지";
        alert.description = "이 페이지에서 암호화폐 채굴 스크립트가 발견되었습니다.";
        alert.recommendation = "페이지를 닫거나 스크립트가 차단되었는지 확인하세요.";
        alert.timestamp = QDateTime::currentDateTime();
        alerts.append(alert);
    }

    // 혼합 콘텐츠
    auto mixed = findMixedContent(html, pageUrl);
    if (!mixed.isEmpty()) {
        SecurityAlert alert;
        alert.severity = SecurityAlert::Medium;
        alert.type = SecurityAlert::MixedContent;
        alert.url = pageUrl;
        alert.title = "혼합 콘텐츠 발견";
        alert.description = QString("%1개의 HTTP 리소스가 HTTPS 페이지에서 로드됩니다.").arg(mixed.size());
        alert.recommendation = "사이트 관리자에게 보고하세요.";
        alert.timestamp = QDateTime::currentDateTime();
        alerts.append(alert);
    }

    // 안전하지 않은 폼
    auto forms = findUnsafeForms(html, pageUrl);
    if (!forms.isEmpty()) {
        SecurityAlert alert;
        alert.severity = SecurityAlert::High;
        alert.type = SecurityAlert::UnsafeForm;
        alert.url = pageUrl;
        alert.title = "안전하지 않은 입력 폼";
        alert.description = "비암호화(HTTP) 서버로 데이터를 전송하는 폼이 있습니다.";
        alert.recommendation = "이 폼에 개인정보를 입력하지 마세요.";
        alert.timestamp = QDateTime::currentDateTime();
        alerts.append(alert);
    }

    // 트래커
    auto trackers = findTrackers(html);
    if (trackers.size() > 3) {
        SecurityAlert alert;
        alert.severity = SecurityAlert::Low;
        alert.type = SecurityAlert::Tracking;
        alert.url = pageUrl;
        alert.title = "다수의 추적기 발견";
        alert.description = QString("%1개의 추적 스크립트가 탐지되었습니다.").arg(trackers.size());
        alert.recommendation = "광고 차단이 활성화되어 있는지 확인하세요.";
        alert.timestamp = QDateTime::currentDateTime();
        alerts.append(alert);
    }

    return alerts;
}

bool SecurityScanner::isPhishing(const QUrl& url) const
{
    QString host = url.host().toLower();
    QString path = url.path().toLower();

    for (const auto& pattern : phishingPatterns()) {
        if (host.contains(pattern) || path.contains(pattern)) return true;
    }

    // 호모그래프 공격 감지 (퓨니코드)
    if (host.contains("xn--")) return true;

    // IP 주소 기반 URL 의심
    QRegularExpression ipRx("^\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}$");
    if (ipRx.match(host).hasMatch()) {
        return true; // IP 기반 URL은 피싱 가능성 높음
    }

    return false;
}

double SecurityScanner::phishingScore(const QUrl& url) const
{
    double score = 0.0;
    QString host = url.host().toLower();

    if (url.scheme() == "http") score += 0.2;
    if (host.contains("xn--")) score += 0.4;
    if (host.count('.') > 3) score += 0.2;
    if (url.path().contains("login") || url.path().contains("signin")) score += 0.1;
    if (host.length() > 30) score += 0.1;

    QRegularExpression ipRx("^\\d+\\.\\d+\\.\\d+\\.\\d+$");
    if (ipRx.match(host).hasMatch()) score += 0.3;

    for (const auto& p : phishingPatterns()) {
        if (host.contains(p)) { score += 0.5; break; }
    }

    return qMin(score, 1.0);
}

bool SecurityScanner::hasCryptominer(const QString& html) const
{
    for (const auto& sig : minerSignatures()) {
        if (html.contains(sig, Qt::CaseInsensitive)) return true;
    }
    return false;
}

QStringList SecurityScanner::findMixedContent(const QString& html, const QUrl& pageUrl) const
{
    QStringList mixed;
    if (pageUrl.scheme() != "https") return mixed;

    QRegularExpression srcRx("(?:src|href)\\s*=\\s*[\"'](http://[^\"']+)[\"']",
                              QRegularExpression::CaseInsensitiveOption);
    auto it = srcRx.globalMatch(html);
    while (it.hasNext()) {
        mixed.append(it.next().captured(1));
    }
    return mixed;
}

QStringList SecurityScanner::findUnsafeForms(const QString& html, const QUrl& pageUrl) const
{
    QStringList unsafe;
    QRegularExpression formRx("<form[^>]*action\\s*=\\s*[\"'](http://[^\"']+)[\"']",
                               QRegularExpression::CaseInsensitiveOption);
    auto it = formRx.globalMatch(html);
    while (it.hasNext()) {
        unsafe.append(it.next().captured(1));
    }
    return unsafe;
}

QStringList SecurityScanner::findTrackers(const QString& html) const
{
    QStringList found;
    for (const auto& tracker : trackerDomains()) {
        if (html.contains(tracker, Qt::CaseInsensitive)) {
            found.append(tracker);
        }
    }
    return found;
}

QString SecurityScanner::generateSecurityScanScript()
{
    return R"(
(function() {
    var results = { forms: [], scripts: [], iframes: [], cookies: 0 };

    // 안전하지 않은 폼
    document.querySelectorAll('form[action^="http://"]').forEach(function(f) {
        results.forms.push(f.action);
    });

    // 외부 스크립트
    document.querySelectorAll('script[src]').forEach(function(s) {
        results.scripts.push(s.src);
    });

    // iframe
    document.querySelectorAll('iframe[src]').forEach(function(i) {
        results.iframes.push(i.src);
    });

    // 쿠키 수
    results.cookies = document.cookie.split(';').filter(function(c) { return c.trim(); }).length;

    console.log('[SecurityScan]', JSON.stringify(results));
    return results;
})();
    )";
}

const QStringList& SecurityScanner::phishingPatterns()
{
    static const QStringList patterns = {
        "login-verify", "account-secure", "signin-update",
        "paypal-secure", "apple-verify", "google-alert",
        "microsoft-verify", "amazon-secure", "bank-login",
        "secure-update", "verify-account", "confirm-identity",
        "suspended-account", "unusual-activity"
    };
    return patterns;
}

const QStringList& SecurityScanner::trackerDomains()
{
    static const QStringList domains = {
        "google-analytics.com", "googletagmanager.com", "facebook.net",
        "doubleclick.net", "googlesyndication.com", "hotjar.com",
        "mixpanel.com", "segment.io", "amplitude.com",
        "fullstory.com", "crazyegg.com", "mouseflow.com",
        "clarity.ms", "newrelic.com", "sentry.io",
        "intercom.io", "drift.com", "hubspot.com",
        "marketo.net", "pardot.com", "optimizely.com"
    };
    return domains;
}

const QStringList& SecurityScanner::minerSignatures()
{
    static const QStringList sigs = {
        "coinhive", "cryptonight", "coin-hive", "coinimp",
        "crypto-loot", "webminepool", "minero.cc",
        "deepminer", "monerominer", "browsermine",
        "ppoi.org", "authedmine"
    };
    return sigs;
}

// ============================================================
// TabGroup
// ============================================================

QJsonObject TabGroup::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["color"] = color.name();
    obj["collapsed"] = collapsed;
    QJsonArray indices;
    for (int idx : tabIndices) indices.append(idx);
    obj["tabIndices"] = indices;
    return obj;
}

TabGroup TabGroup::fromJson(const QJsonObject& obj)
{
    TabGroup g;
    g.id = obj["id"].toInteger(-1);
    g.name = obj["name"].toString();
    g.color = QColor(obj["color"].toString("#4285f4"));
    g.collapsed = obj["collapsed"].toBool();
    for (const auto& v : obj["tabIndices"].toArray()) {
        g.tabIndices.append(v.toInt());
    }
    return g;
}

// ============================================================
// TabGroupManager
// ============================================================

TabGroupManager::TabGroupManager(QObject* parent)
    : QObject(parent)
{
}

int64_t TabGroupManager::createGroup(const QString& name, const QColor& color)
{
    TabGroup g;
    g.id = m_nextId++;
    g.name = name;
    g.color = color;
    m_groups.append(g);
    emit groupCreated(g);
    return g.id;
}

bool TabGroupManager::removeGroup(int64_t id)
{
    for (int i = 0; i < m_groups.size(); ++i) {
        if (m_groups[i].id == id) {
            m_groups.removeAt(i);
            emit groupRemoved(id);
            return true;
        }
    }
    return false;
}

bool TabGroupManager::addTabToGroup(int64_t groupId, int tabIndex)
{
    auto* g = findGroup(groupId);
    if (!g) return false;
    if (!g->tabIndices.contains(tabIndex)) {
        g->tabIndices.append(tabIndex);
        emit groupUpdated(*g);
    }
    return true;
}

bool TabGroupManager::removeTabFromGroup(int64_t groupId, int tabIndex)
{
    auto* g = findGroup(groupId);
    if (!g) return false;
    g->tabIndices.removeAll(tabIndex);
    emit groupUpdated(*g);
    return true;
}

bool TabGroupManager::renameGroup(int64_t groupId, const QString& name)
{
    auto* g = findGroup(groupId);
    if (!g) return false;
    g->name = name;
    emit groupUpdated(*g);
    return true;
}

bool TabGroupManager::setGroupColor(int64_t groupId, const QColor& color)
{
    auto* g = findGroup(groupId);
    if (!g) return false;
    g->color = color;
    emit groupUpdated(*g);
    return true;
}

bool TabGroupManager::toggleCollapse(int64_t groupId)
{
    auto* g = findGroup(groupId);
    if (!g) return false;
    g->collapsed = !g->collapsed;
    emit groupUpdated(*g);
    return true;
}

TabGroup* TabGroupManager::findGroup(int64_t id)
{
    for (auto& g : m_groups) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

TabGroup* TabGroupManager::groupForTab(int tabIndex)
{
    for (auto& g : m_groups) {
        if (g.tabIndices.contains(tabIndex)) return &g;
    }
    return nullptr;
}

QJsonArray TabGroupManager::toJson() const
{
    QJsonArray arr;
    for (const auto& g : m_groups) arr.append(g.toJson());
    return arr;
}

void TabGroupManager::fromJson(const QJsonArray& arr)
{
    m_groups.clear();
    for (const auto& v : arr) {
        auto g = TabGroup::fromJson(v.toObject());
        m_groups.append(g);
        if (g.id >= m_nextId) m_nextId = g.id + 1;
    }
}

QList<QColor> TabGroupManager::defaultColors()
{
    return {
        QColor("#4285f4"),  // Blue
        QColor("#ea4335"),  // Red
        QColor("#34a853"),  // Green
        QColor("#fbbc05"),  // Yellow
        QColor("#ff6d01"),  // Orange
        QColor("#46bdc6"),  // Teal
        QColor("#a142f4"),  // Purple
        QColor("#f538a0"),  // Pink
    };
}

// ============================================================
// BrowserProfileManager
// ============================================================

BrowserProfileManager::BrowserProfileManager(const QString& basePath, QObject* parent)
    : QObject(parent)
    , m_basePath(basePath)
{
    QDir().mkpath(basePath);
}

QWebEngineProfile* BrowserProfileManager::createProfile(const QString& name, ProfileType type)
{
    if (m_profiles.contains(name)) return m_profiles[name];

    QWebEngineProfile* profile = nullptr;

    if (type == Normal) {
        QString storagePath = m_basePath + "/profiles/" + name;
        QDir().mkpath(storagePath);
        profile = new QWebEngineProfile(name, this);
        profile->setPersistentStoragePath(storagePath);
        profile->setCachePath(storagePath + "/cache");
    } else {
        // 시크릿/게스트: off-the-record
        profile = new QWebEngineProfile(this);
    }

    setupProfile(profile, type);
    m_profiles[name] = profile;
    return profile;
}

QWebEngineProfile* BrowserProfileManager::getProfile(const QString& name) const
{
    return m_profiles.value(name, nullptr);
}

QWebEngineProfile* BrowserProfileManager::incognitoProfile()
{
    if (!m_incognito) {
        m_incognito = new QWebEngineProfile(this); // off-the-record
        setupProfile(m_incognito, Incognito);
    }
    return m_incognito;
}

QWebEngineProfile* BrowserProfileManager::guestProfile()
{
    if (!m_guest) {
        m_guest = new QWebEngineProfile(this);
        setupProfile(m_guest, Guest);
    }
    return m_guest;
}

QStringList BrowserProfileManager::profileNames() const
{
    return m_profiles.keys();
}

bool BrowserProfileManager::removeProfile(const QString& name)
{
    if (name == "default") return false;
    if (!m_profiles.contains(name)) return false;

    auto* profile = m_profiles.take(name);
    profile->deleteLater();

    // 스토리지 삭제
    QDir dir(m_basePath + "/profiles/" + name);
    dir.removeRecursively();
    return true;
}

bool BrowserProfileManager::switchProfile(const QString& name)
{
    if (!m_profiles.contains(name)) return false;
    m_currentProfile = name;
    emit profileChanged(name);
    return true;
}

void BrowserProfileManager::setupProfile(QWebEngineProfile* profile, ProfileType type)
{
    if (!profile) return;

    // 공통 설정
    profile->setHttpUserAgent(
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "OrdinalV8/1.2.0 Chrome/120.0.0.0 Safari/537.36");

    auto* settings = profile->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, type != Guest);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    settings->setAttribute(QWebEngineSettings::AutoLoadImages, true);
    settings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);

    if (type == Incognito || type == Guest) {
        settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, false);
    }
}

} // namespace Engine
} // namespace Ordinal
