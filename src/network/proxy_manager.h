#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QList>
#include <QHash>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <functional>

namespace Ordinal {
namespace Network {

// Proxy protocol types
enum class ProxyProtocol {
    None,
    HTTP,
    HTTPS,
    SOCKS4,
    SOCKS5,
    PAC          // Proxy Auto-Config
};

// A proxy server entry
struct ProxyConfig {
    ProxyProtocol protocol = ProxyProtocol::None;
    QString host;
    int port = 8080;
    QString username;
    QString password;
    bool enabled = true;

    // Convert to QNetworkProxy
    QNetworkProxy toQNetworkProxy() const;
};

// Per-domain proxy rule
struct ProxyRule {
    QString pattern;           // glob or regex pattern for domain/URL
    bool isRegex = false;
    ProxyConfig proxy;         // proxy to use; protocol=None means direct
    bool bypass = false;       // true = bypass proxy for this pattern
};

class ProxyManager : public QObject {
    Q_OBJECT
public:
    explicit ProxyManager(QObject* parent = nullptr);
    ~ProxyManager() override;

    // Global proxy configuration
    void setGlobalProxy(const ProxyConfig& config);
    ProxyConfig globalProxy() const;
    void clearGlobalProxy();

    // PAC file support
    void setPacUrl(const QUrl& url);
    QUrl pacUrl() const;
    void setPacScript(const QString& script);
    QString pacScript() const;
    void reloadPac();

    // Per-domain rules
    void addRule(const ProxyRule& rule);
    void removeRule(int index);
    void clearRules();
    QList<ProxyRule> rules() const;

    // Bypass list (no proxy for these)
    void addBypassHost(const QString& pattern);
    void removeBypassHost(const QString& pattern);
    QStringList bypassList() const;

    // Resolve proxy for a given URL
    // Returns the QNetworkProxy to use (may be QNetworkProxy::NoProxy)
    QNetworkProxy proxyForUrl(const QUrl& url) const;
    QList<QNetworkProxy> proxiesForUrl(const QUrl& url) const;

    // Apply proxy to a QNetworkAccessManager
    void applyToNetworkManager(QObject* nam);

    // Authentication
    void setProxyCredentials(const QString& host, int port,
                              const QString& user, const QString& password);
    QPair<QString,QString> credentialsFor(const QString& host, int port) const;

    // Enable/disable
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // Import/export settings
    QVariantMap toVariantMap() const;
    void fromVariantMap(const QVariantMap& map);

signals:
    void proxyChanged();
    void authenticationRequired(const QString& host, int port,
                                 QString* user, QString* password);
    void pacLoaded(bool success, const QString& error);

private:
    // PAC evaluation
    QString evaluatePac(const QUrl& url) const;
    QList<QNetworkProxy> parsePacResult(const QString& result) const;

    // Pattern matching
    bool matchesPattern(const QString& pattern, const QString& host,
                        bool isRegex) const;

    // Data
    bool m_enabled = true;
    ProxyConfig m_globalProxy;
    QUrl m_pacUrl;
    QString m_pacScript;
    QList<ProxyRule> m_rules;
    QStringList m_bypassList;
    QHash<QString, QPair<QString,QString>> m_credentials; // "host:port" -> (user, pass)
};

} // namespace Network
} // namespace Ordinal
