#include "proxy_manager.h"

#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDebug>

namespace Ordinal {
namespace Network {

// -----------------------------------------------------------------------
// ProxyConfig helpers
// -----------------------------------------------------------------------
QNetworkProxy ProxyConfig::toQNetworkProxy() const {
    if (!enabled || protocol == ProxyProtocol::None)
        return QNetworkProxy(QNetworkProxy::NoProxy);

    QNetworkProxy::ProxyType qt_type;
    switch (protocol) {
        case ProxyProtocol::HTTP:
        case ProxyProtocol::HTTPS:
            qt_type = QNetworkProxy::HttpProxy;
            break;
        case ProxyProtocol::SOCKS4:
            qt_type = QNetworkProxy::Socks5Proxy; // Qt doesn't differentiate
            break;
        case ProxyProtocol::SOCKS5:
            qt_type = QNetworkProxy::Socks5Proxy;
            break;
        default:
            return QNetworkProxy(QNetworkProxy::NoProxy);
    }

    QNetworkProxy proxy(qt_type, host, static_cast<quint16>(port));
    if (!username.isEmpty()) {
        proxy.setUser(username);
        proxy.setPassword(password);
    }
    return proxy;
}

// -----------------------------------------------------------------------
// ProxyManager — a QNetworkProxyFactory subclass for per-URL resolution
// -----------------------------------------------------------------------
class OrdinalProxyFactory : public QNetworkProxyFactory {
public:
    explicit OrdinalProxyFactory(const ProxyManager* mgr) : m_mgr(mgr) {}
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery& query) override {
        return m_mgr->proxiesForUrl(query.url());
    }
private:
    const ProxyManager* m_mgr;
};

// -----------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------
ProxyManager::ProxyManager(QObject* parent)
    : QObject(parent)
{
    // Default bypass list
    m_bypassList << "localhost" << "127.0.0.1" << "::1" << "*.local";
}

ProxyManager::~ProxyManager() = default;

// -----------------------------------------------------------------------
// Global proxy
// -----------------------------------------------------------------------
void ProxyManager::setGlobalProxy(const ProxyConfig& config) {
    m_globalProxy = config;
    emit proxyChanged();
}

ProxyConfig ProxyManager::globalProxy() const { return m_globalProxy; }

void ProxyManager::clearGlobalProxy() {
    m_globalProxy = ProxyConfig{};
    emit proxyChanged();
}

// -----------------------------------------------------------------------
// PAC support
// -----------------------------------------------------------------------
void ProxyManager::setPacUrl(const QUrl& url) {
    m_pacUrl = url;
    reloadPac();
}

QUrl ProxyManager::pacUrl() const { return m_pacUrl; }

void ProxyManager::setPacScript(const QString& script) {
    m_pacScript = script;
    emit proxyChanged();
}

QString ProxyManager::pacScript() const { return m_pacScript; }

void ProxyManager::reloadPac() {
    if (m_pacUrl.isLocalFile()) {
        QFile f(m_pacUrl.toLocalFile());
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_pacScript = QString::fromUtf8(f.readAll());
            emit pacLoaded(true, {});
        } else {
            emit pacLoaded(false, "Cannot open PAC file: " + m_pacUrl.toString());
        }
    } else {
        // HTTP fetch would go here; for now, emit a placeholder
        qWarning() << "[ProxyManager] Remote PAC URL not yet implemented:" << m_pacUrl;
        emit pacLoaded(false, "Remote PAC fetch not implemented");
    }
}

// -----------------------------------------------------------------------
// Per-domain rules
// -----------------------------------------------------------------------
void ProxyManager::addRule(const ProxyRule& rule) {
    m_rules << rule;
    emit proxyChanged();
}

void ProxyManager::removeRule(int index) {
    if (index >= 0 && index < m_rules.size()) {
        m_rules.removeAt(index);
        emit proxyChanged();
    }
}

void ProxyManager::clearRules() {
    m_rules.clear();
    emit proxyChanged();
}

QList<ProxyRule> ProxyManager::rules() const { return m_rules; }

// -----------------------------------------------------------------------
// Bypass list
// -----------------------------------------------------------------------
void ProxyManager::addBypassHost(const QString& pattern) {
    if (!m_bypassList.contains(pattern)) {
        m_bypassList << pattern;
        emit proxyChanged();
    }
}

void ProxyManager::removeBypassHost(const QString& pattern) {
    if (m_bypassList.removeOne(pattern))
        emit proxyChanged();
}

QStringList ProxyManager::bypassList() const { return m_bypassList; }

// -----------------------------------------------------------------------
// Core: resolve proxy for URL
// -----------------------------------------------------------------------
QNetworkProxy ProxyManager::proxyForUrl(const QUrl& url) const {
    auto list = proxiesForUrl(url);
    return list.isEmpty() ? QNetworkProxy(QNetworkProxy::NoProxy) : list.first();
}

QList<QNetworkProxy> ProxyManager::proxiesForUrl(const QUrl& url) const {
    if (!m_enabled)
        return { QNetworkProxy(QNetworkProxy::NoProxy) };

    QString host = url.host();

    // 1. Check bypass list first
    for (const QString& pattern : m_bypassList) {
        if (matchesPattern(pattern, host, false))
            return { QNetworkProxy(QNetworkProxy::NoProxy) };
    }

    // 2. Check per-domain rules (first match wins)
    for (const ProxyRule& rule : m_rules) {
        if (matchesPattern(rule.pattern, host, rule.isRegex)) {
            if (rule.bypass)
                return { QNetworkProxy(QNetworkProxy::NoProxy) };
            return { rule.proxy.toQNetworkProxy() };
        }
    }

    // 3. PAC script
    if (!m_pacScript.isEmpty()) {
        QString result = evaluatePac(url);
        if (!result.isEmpty())
            return parsePacResult(result);
    }

    // 4. Global proxy
    if (m_globalProxy.protocol != ProxyProtocol::None && m_globalProxy.enabled)
        return { m_globalProxy.toQNetworkProxy() };

    return { QNetworkProxy(QNetworkProxy::NoProxy) };
}

// -----------------------------------------------------------------------
// Apply to QNetworkAccessManager
// -----------------------------------------------------------------------
void ProxyManager::applyToNetworkManager(QObject* nam) {
    auto* qnam = qobject_cast<QNetworkAccessManager*>(nam);
    if (!qnam) return;

    if (m_globalProxy.protocol != ProxyProtocol::None && m_enabled) {
        qnam->setProxy(m_globalProxy.toQNetworkProxy());
    } else {
        qnam->setProxyFactory(new OrdinalProxyFactory(this));
    }
}

// -----------------------------------------------------------------------
// Authentication
// -----------------------------------------------------------------------
void ProxyManager::setProxyCredentials(const QString& host, int port,
                                        const QString& user, const QString& password) {
    QString key = QString("%1:%2").arg(host).arg(port);
    m_credentials[key] = { user, password };
}

QPair<QString,QString> ProxyManager::credentialsFor(const QString& host, int port) const {
    QString key = QString("%1:%2").arg(host).arg(port);
    return m_credentials.value(key);
}

// -----------------------------------------------------------------------
// Enable / disable
// -----------------------------------------------------------------------
void ProxyManager::setEnabled(bool enabled) {
    m_enabled = enabled;
    emit proxyChanged();
}

bool ProxyManager::isEnabled() const { return m_enabled; }

// -----------------------------------------------------------------------
// Serialization
// -----------------------------------------------------------------------
QVariantMap ProxyManager::toVariantMap() const {
    QVariantMap map;
    map["enabled"] = m_enabled;
    map["pacUrl"]  = m_pacUrl.toString();

    QVariantMap gp;
    gp["protocol"] = static_cast<int>(m_globalProxy.protocol);
    gp["host"]     = m_globalProxy.host;
    gp["port"]     = m_globalProxy.port;
    gp["username"] = m_globalProxy.username;
    // Don't serialize password in plaintext — caller can handle encryption
    map["globalProxy"] = gp;

    QVariantList bypassVar;
    for (const QString& b : m_bypassList) bypassVar << b;
    map["bypass"] = bypassVar;

    QVariantList rulesVar;
    for (const ProxyRule& r : m_rules) {
        QVariantMap rm;
        rm["pattern"] = r.pattern;
        rm["isRegex"] = r.isRegex;
        rm["bypass"]  = r.bypass;
        rm["protocol"] = static_cast<int>(r.proxy.protocol);
        rm["host"]     = r.proxy.host;
        rm["port"]     = r.proxy.port;
        rm["username"] = r.proxy.username;
        rulesVar << rm;
    }
    map["rules"] = rulesVar;

    return map;
}

void ProxyManager::fromVariantMap(const QVariantMap& map) {
    m_enabled = map.value("enabled", true).toBool();
    m_pacUrl  = QUrl(map.value("pacUrl").toString());

    QVariantMap gp = map.value("globalProxy").toMap();
    m_globalProxy.protocol = static_cast<ProxyProtocol>(gp.value("protocol", 0).toInt());
    m_globalProxy.host     = gp.value("host").toString();
    m_globalProxy.port     = gp.value("port", 8080).toInt();
    m_globalProxy.username = gp.value("username").toString();

    m_bypassList.clear();
    for (const QVariant& v : map.value("bypass").toList())
        m_bypassList << v.toString();

    m_rules.clear();
    for (const QVariant& rv : map.value("rules").toList()) {
        QVariantMap rm = rv.toMap();
        ProxyRule rule;
        rule.pattern  = rm.value("pattern").toString();
        rule.isRegex  = rm.value("isRegex", false).toBool();
        rule.bypass   = rm.value("bypass", false).toBool();
        rule.proxy.protocol = static_cast<ProxyProtocol>(rm.value("protocol", 0).toInt());
        rule.proxy.host     = rm.value("host").toString();
        rule.proxy.port     = rm.value("port", 8080).toInt();
        rule.proxy.username = rm.value("username").toString();
        m_rules << rule;
    }

    emit proxyChanged();
}

// -----------------------------------------------------------------------
// PAC evaluation (minimal stub — full JS PAC requires JS engine integration)
// -----------------------------------------------------------------------
QString ProxyManager::evaluatePac(const QUrl& url) const {
    // Full PAC support requires running FindProxyForURL() in a JS engine.
    // For now: parse simple "PROXY host:port" or "DIRECT" strings from the script.
    // A real implementation would use V8/QtQml to evaluate the JS.
    Q_UNUSED(url);
    if (m_pacScript.contains("DIRECT")) return "DIRECT";
    QRegularExpression re(R"(PROXY\s+([\w\.\-]+:\d+))");
    auto match = re.match(m_pacScript);
    if (match.hasMatch())
        return "PROXY " + match.captured(1);
    return {};
}

QList<QNetworkProxy> ProxyManager::parsePacResult(const QString& result) const {
    QList<QNetworkProxy> proxies;
    QStringList parts = result.split(';');
    for (const QString& part : parts) {
        QString trimmed = part.trimmed();
        if (trimmed == "DIRECT") {
            proxies << QNetworkProxy(QNetworkProxy::NoProxy);
        } else if (trimmed.startsWith("PROXY ")) {
            QString hostPort = trimmed.mid(6).trimmed();
            QStringList hp = hostPort.split(':');
            if (hp.size() == 2) {
                QNetworkProxy p(QNetworkProxy::HttpProxy,
                                hp[0], static_cast<quint16>(hp[1].toInt()));
                proxies << p;
            }
        } else if (trimmed.startsWith("SOCKS ")) {
            QString hostPort = trimmed.mid(6).trimmed();
            QStringList hp = hostPort.split(':');
            if (hp.size() == 2) {
                QNetworkProxy p(QNetworkProxy::Socks5Proxy,
                                hp[0], static_cast<quint16>(hp[1].toInt()));
                proxies << p;
            }
        }
    }
    if (proxies.isEmpty())
        proxies << QNetworkProxy(QNetworkProxy::NoProxy);
    return proxies;
}

// -----------------------------------------------------------------------
// Pattern matching (glob-style)
// -----------------------------------------------------------------------
bool ProxyManager::matchesPattern(const QString& pattern,
                                   const QString& host,
                                   bool isRegex) const {
    if (isRegex) {
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        return re.match(host).hasMatch();
    }

    // Glob: * matches any sequence, ? matches one char
    QString regexStr = QRegularExpression::escape(pattern);
    regexStr.replace("\\*", ".*");
    regexStr.replace("\\?", ".");
    QRegularExpression re("^" + regexStr + "$",
                          QRegularExpression::CaseInsensitiveOption);
    return re.match(host).hasMatch();
}

} // namespace Network
} // namespace Ordinal
