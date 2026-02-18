#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QCache>
#include <QTimer>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <functional>
#include <memory>

namespace Ordinal {
namespace Network {

// DNS Record Types
enum class DnsRecordType {
    A,      // IPv4 address
    AAAA,   // IPv6 address
    CNAME,  // Canonical name
    MX,     // Mail exchange
    TXT,    // Text record
    NS,     // Name server
    SOA     // Start of authority
};

// A single DNS record
struct DnsRecord {
    DnsRecordType type;
    QString name;
    QString value;
    int ttl = 300;          // seconds
    int priority = 0;       // for MX records
    bool dnssecValid = false;
    QDateTime expires;
};

// Result of a DNS lookup
struct DnsResult {
    QString domain;
    QList<DnsRecord> records;
    bool fromCache = false;
    bool dnssecValidated = false;
    bool isMalicious = false;
    bool rebindingRisk = false;
    QString errorMessage;
    bool success = false;
};

// Cache entry
struct DnsCacheEntry {
    QList<DnsRecord> records;
    QDateTime insertedAt;
    QDateTime expiresAt;
    bool dnssecValidated = false;
};

// DNS-over-HTTPS provider
struct DoHProvider {
    QString name;
    QString url;          // template, use {name} placeholder
    bool enabled = true;
};

// Proxy for DNS (not network proxy — just for thread signals)
class DNSResolver : public QObject {
    Q_OBJECT
public:
    explicit DNSResolver(QObject* parent = nullptr);
    ~DNSResolver() override;

    // Async lookup — emits resolved() or failed() when done
    void resolve(const QString& domain,
                 DnsRecordType type = DnsRecordType::A);

    // Lookup with callback
    void resolve(const QString& domain,
                 DnsRecordType type,
                 std::function<void(const DnsResult&)> callback);

    // Synchronous lookup (blocks; prefer async in UI)
    DnsResult resolveSync(const QString& domain,
                          DnsRecordType type = DnsRecordType::A);

    // Configuration
    void setDoHEnabled(bool enabled);
    bool isDoHEnabled() const;

    void setCustomDnsServer(const QString& server, int port = 53);
    QString customDnsServer() const;

    void addDoHProvider(const DoHProvider& provider);
    void setActiveDoHProvider(const QString& name);

    // Cache control
    void clearCache();
    int cacheSize() const;
    void setCacheMaxSize(int entries);

    // Threat database
    void addMaliciousDomain(const QString& domain);
    void loadMaliciousDomainsFromFile(const QString& filePath);
    bool isMaliciousDomain(const QString& domain) const;

    // DNS rebinding detection
    bool detectRebinding(const QString& domain, const QStringList& resolvedIPs);

    // DNSSEC
    void setDnssecRequired(bool required);
    bool isDnssecRequired() const;

signals:
    void resolved(const DnsResult& result);
    void failed(const QString& domain, const QString& error);
    void maliciousDomainDetected(const QString& domain);
    void rebindingAttackDetected(const QString& domain, const QString& ip);

private slots:
    void onDoHReplyFinished(QNetworkReply* reply);
    void onCacheCleanupTimer();

private:
    // DoH lookup
    void resolveViaDoH(const QString& domain,
                       DnsRecordType type,
                       std::function<void(const DnsResult&)> callback);

    // System DNS lookup
    void resolveViaSystem(const QString& domain,
                          DnsRecordType type,
                          std::function<void(const DnsResult&)> callback);

    // Parse DoH JSON response
    DnsResult parseDoHResponse(const QString& domain,
                                DnsRecordType type,
                                const QByteArray& json);

    // Cache helpers
    bool lookupCache(const QString& key, DnsResult& result);
    void storeCache(const QString& key, const DnsResult& result);
    QString cacheKey(const QString& domain, DnsRecordType type) const;

    // Record type helpers
    QString recordTypeName(DnsRecordType type) const;
    int recordTypeNumber(DnsRecordType type) const;

    // Private IP range check (for rebinding detection)
    bool isPrivateOrLoopback(const QString& ip) const;
    bool isPublicDomain(const QString& domain) const;

    // Data members
    bool m_doHEnabled = true;
    bool m_dnssecRequired = false;
    QString m_customDnsServer;
    int m_customDnsPort = 53;

    QList<DoHProvider> m_doHProviders;
    int m_activeProviderIndex = 0;

    QHash<QString, DnsCacheEntry> m_cache;
    int m_cacheMaxSize = 1000;

    QHash<QString, QDateTime> m_maliciousDomains;    // domain -> reported date
    QSet<QString> m_maliciousSet;                    // fast lookup set

    // Track known IPs per domain for rebinding detection
    QHash<QString, QStringList> m_domainIpHistory;

    QNetworkAccessManager* m_nam = nullptr;
    QTimer* m_cacheCleanupTimer = nullptr;

    // Pending callbacks keyed by reply pointer
    QHash<QNetworkReply*, QPair<QString, std::function<void(const DnsResult&)>>> m_pendingCallbacks;
};

} // namespace Network
} // namespace Ordinal
