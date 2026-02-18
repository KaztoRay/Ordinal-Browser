#include "dns_resolver.h"

#include <QCoreApplication>
#include <QHostInfo>
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QMutex>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrent>

namespace Ordinal {
namespace Network {

// -----------------------------------------------------------------------
// Default DoH providers
// -----------------------------------------------------------------------
static const QList<DoHProvider> kDefaultProviders = {
    { "Cloudflare", "https://cloudflare-dns.com/dns-query", true },
    { "Google",     "https://dns.google/resolve",           true },
    { "Quad9",      "https://dns.quad9.net/dns-query",      true },
};

// Private IP CIDRs for rebinding detection
static bool isRFC1918(const QString& ip) {
    if (ip.startsWith("10."))          return true;
    if (ip.startsWith("192.168."))     return true;
    if (ip == "127.0.0.1" || ip == "::1") return true;
    // 172.16.0.0/12
    QStringList parts = ip.split('.');
    if (parts.size() == 4 && parts[0] == "172") {
        int second = parts[1].toInt();
        if (second >= 16 && second <= 31) return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------
DNSResolver::DNSResolver(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_cacheCleanupTimer(new QTimer(this))
{
    m_doHProviders = kDefaultProviders;

    connect(m_nam, &QNetworkAccessManager::finished,
            this, &DNSResolver::onDoHReplyFinished);

    // Clean expired cache entries every 5 minutes
    connect(m_cacheCleanupTimer, &QTimer::timeout,
            this, &DNSResolver::onCacheCleanupTimer);
    m_cacheCleanupTimer->start(5 * 60 * 1000);
}

DNSResolver::~DNSResolver() = default;

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------
void DNSResolver::resolve(const QString& domain, DnsRecordType type) {
    resolve(domain, type, [this](const DnsResult& result) {
        if (result.success)
            emit resolved(result);
        else
            emit failed(result.domain, result.errorMessage);
    });
}

void DNSResolver::resolve(const QString& domain,
                          DnsRecordType type,
                          std::function<void(const DnsResult&)> callback) {
    // 1. Check cache
    DnsResult cached;
    if (lookupCache(cacheKey(domain, type), cached)) {
        if (callback) callback(cached);
        return;
    }

    // 2. Check threat database
    if (isMaliciousDomain(domain)) {
        DnsResult bad;
        bad.domain = domain;
        bad.isMalicious = true;
        bad.success = false;
        bad.errorMessage = "Domain is in malicious domain list";
        emit maliciousDomainDetected(domain);
        if (callback) callback(bad);
        return;
    }

    // 3. Route to DoH or system
    if (m_doHEnabled) {
        resolveViaDoH(domain, type, callback);
    } else {
        resolveViaSystem(domain, type, callback);
    }
}

DnsResult DNSResolver::resolveSync(const QString& domain, DnsRecordType type) {
    DnsResult result;
    QEventLoop loop;
    resolve(domain, type, [&](const DnsResult& r) {
        result = r;
        loop.quit();
    });
    loop.exec();
    return result;
}

// -----------------------------------------------------------------------
// DoH resolution
// -----------------------------------------------------------------------
void DNSResolver::resolveViaDoH(const QString& domain,
                                 DnsRecordType type,
                                 std::function<void(const DnsResult&)> callback) {
    if (m_doHProviders.isEmpty()) {
        resolveViaSystem(domain, type, callback);
        return;
    }

    const DoHProvider& provider = m_doHProviders.value(m_activeProviderIndex);
    QString url = provider.url;

    // Build query URL
    // Cloudflare & Google use ?name=&type= style
    QString typeStr = recordTypeName(type);
    QString queryUrl;
    if (url.contains("cloudflare-dns.com") || url.contains("dns.google")) {
        queryUrl = QString("%1?name=%2&type=%3")
                       .arg(url, domain, typeStr);
    } else {
        queryUrl = QString("%1?name=%2&type=%3")
                       .arg(url, domain, typeStr);
    }

    QNetworkRequest req(queryUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/dns-json");
    req.setRawHeader("Accept", "application/dns-json");

    QNetworkReply* reply = m_nam->get(req);
    m_pendingCallbacks[reply] = { domain, callback };
}

void DNSResolver::onDoHReplyFinished(QNetworkReply* reply) {
    reply->deleteLater();

    auto it = m_pendingCallbacks.find(reply);
    if (it == m_pendingCallbacks.end()) return;

    QString domain = it->first;
    auto callback  = it->second;
    m_pendingCallbacks.erase(it);

    if (reply->error() != QNetworkReply::NoError) {
        // Fall back to system DNS
        resolveViaSystem(domain, DnsRecordType::A, callback);
        return;
    }

    QByteArray data = reply->readAll();
    // We don't know the type from just the reply — store it in the pending map
    // For simplicity here we parse and detect from response
    DnsResult result = parseDoHResponse(domain, DnsRecordType::A, data);

    if (!result.success) {
        resolveViaSystem(domain, DnsRecordType::A, callback);
        return;
    }

    // Rebinding detection
    QStringList ips;
    for (const auto& rec : result.records) {
        if (rec.type == DnsRecordType::A || rec.type == DnsRecordType::AAAA)
            ips << rec.value;
    }
    if (!ips.isEmpty() && detectRebinding(domain, ips)) {
        result.rebindingRisk = true;
        for (const auto& ip : ips) {
            if (isRFC1918(ip))
                emit rebindingAttackDetected(domain, ip);
        }
    }

    storeCache(cacheKey(domain, DnsRecordType::A), result);

    if (callback) callback(result);
    emit resolved(result);
}

// -----------------------------------------------------------------------
// System DNS (fallback)
// -----------------------------------------------------------------------
void DNSResolver::resolveViaSystem(const QString& domain,
                                    DnsRecordType type,
                                    std::function<void(const DnsResult&)> callback) {
    // Only A/AAAA supported via QHostInfo
    QtConcurrent::run([=]() {
        QHostInfo info = QHostInfo::fromName(domain);

        DnsResult result;
        result.domain = domain;

        if (info.error() != QHostInfo::NoError) {
            result.success = false;
            result.errorMessage = info.errorString();
            QMetaObject::invokeMethod(this, [=]() {
                if (callback) callback(result);
                emit failed(domain, result.errorMessage);
            }, Qt::QueuedConnection);
            return;
        }

        for (const QHostAddress& addr : info.addresses()) {
            DnsRecord rec;
            rec.name = domain;
            rec.value = addr.toString();
            rec.type = addr.protocol() == QAbstractSocket::IPv6Protocol
                           ? DnsRecordType::AAAA
                           : DnsRecordType::A;
            rec.ttl = 300;
            rec.expires = QDateTime::currentDateTimeUtc().addSecs(300);
            result.records << rec;
        }
        result.success = true;

        QMetaObject::invokeMethod(this, [=]() {
            storeCache(cacheKey(domain, type), result);
            if (callback) callback(result);
            emit resolved(result);
        }, Qt::QueuedConnection);
    });
}

// -----------------------------------------------------------------------
// Parse DoH JSON response
// -----------------------------------------------------------------------
DnsResult DNSResolver::parseDoHResponse(const QString& domain,
                                         DnsRecordType /*type*/,
                                         const QByteArray& json) {
    DnsResult result;
    result.domain = domain;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError) {
        result.success = false;
        result.errorMessage = "JSON parse error: " + err.errorString();
        return result;
    }

    QJsonObject obj = doc.object();

    // Status 0 = NOERROR
    int status = obj.value("Status").toInt(-1);
    if (status != 0) {
        result.success = false;
        result.errorMessage = QString("DNS status %1").arg(status);
        return result;
    }

    // Check AD (Authenticated Data = DNSSEC validated)
    result.dnssecValidated = obj.value("AD").toBool(false);

    // Parse Answer array
    QJsonArray answers = obj.value("Answer").toArray();
    for (const QJsonValue& av : answers) {
        QJsonObject ao = av.toObject();
        DnsRecord rec;
        rec.name = ao.value("name").toString();
        rec.ttl  = ao.value("TTL").toInt(300);
        rec.value = ao.value("data").toString();
        rec.dnssecValid = result.dnssecValidated;
        rec.expires = QDateTime::currentDateTimeUtc().addSecs(rec.ttl);

        int t = ao.value("type").toInt(1);
        switch (t) {
            case 1:  rec.type = DnsRecordType::A;     break;
            case 2:  rec.type = DnsRecordType::NS;    break;
            case 5:  rec.type = DnsRecordType::CNAME; break;
            case 6:  rec.type = DnsRecordType::SOA;   break;
            case 15: rec.type = DnsRecordType::MX;    break;
            case 16: rec.type = DnsRecordType::TXT;   break;
            case 28: rec.type = DnsRecordType::AAAA;  break;
            default: rec.type = DnsRecordType::A;     break;
        }
        result.records << rec;
    }

    result.success = !result.records.isEmpty();
    if (!result.success)
        result.errorMessage = "No records in response";

    return result;
}

// -----------------------------------------------------------------------
// DNS Rebinding Detection
// -----------------------------------------------------------------------
bool DNSResolver::detectRebinding(const QString& domain, const QStringList& ips) {
    if (!isPublicDomain(domain)) return false;

    for (const QString& ip : ips) {
        if (isRFC1918(ip)) {
            return true;
        }
    }

    // Check IP history — if domain previously resolved to public IP
    // and now resolves to private, that's rebinding
    QStringList& history = m_domainIpHistory[domain];
    bool hadPublic = false;
    for (const QString& h : history)
        if (!isRFC1918(h)) hadPublic = true;

    if (hadPublic) {
        for (const QString& ip : ips)
            if (isRFC1918(ip)) return true;
    }

    // Update history (keep last 10)
    history << ips;
    if (history.size() > 20) history = history.mid(history.size() - 20);

    return false;
}

bool DNSResolver::isPrivateOrLoopback(const QString& ip) const {
    return isRFC1918(ip);
}

bool DNSResolver::isPublicDomain(const QString& domain) const {
    // Exclude localhost, .local, .internal etc.
    static const QStringList localTLDs = {
        "localhost", ".local", ".internal", ".lan", ".intranet",
        ".home", ".corp", ".private"
    };
    for (const QString& tld : localTLDs)
        if (domain == tld || domain.endsWith(tld)) return false;
    return true;
}

// -----------------------------------------------------------------------
// Cache
// -----------------------------------------------------------------------
QString DNSResolver::cacheKey(const QString& domain, DnsRecordType type) const {
    return domain + ":" + recordTypeName(type);
}

bool DNSResolver::lookupCache(const QString& key, DnsResult& result) {
    auto it = m_cache.find(key);
    if (it == m_cache.end()) return false;

    const DnsCacheEntry& entry = it.value();
    if (QDateTime::currentDateTimeUtc() > entry.expiresAt) {
        m_cache.remove(key);
        return false;
    }

    result.records = entry.records;
    result.dnssecValidated = entry.dnssecValidated;
    result.fromCache = true;
    result.success = true;
    result.domain = key.split(':').first();
    return true;
}

void DNSResolver::storeCache(const QString& key, const DnsResult& result) {
    if (!result.success) return;

    // Evict if too large (simple FIFO — remove oldest)
    if (m_cache.size() >= m_cacheMaxSize) {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (it->insertedAt < oldest->insertedAt) oldest = it;
        }
        m_cache.erase(oldest);
    }

    int minTtl = 300;
    for (const auto& r : result.records)
        minTtl = qMin(minTtl, r.ttl);

    DnsCacheEntry entry;
    entry.records = result.records;
    entry.insertedAt = QDateTime::currentDateTimeUtc();
    entry.expiresAt = entry.insertedAt.addSecs(minTtl);
    entry.dnssecValidated = result.dnssecValidated;
    m_cache[key] = entry;
}

void DNSResolver::onCacheCleanupTimer() {
    QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList toRemove;
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (now > it->expiresAt) toRemove << it.key();
    }
    for (const QString& k : toRemove) m_cache.remove(k);
}

void DNSResolver::clearCache() { m_cache.clear(); }
int DNSResolver::cacheSize() const { return m_cache.size(); }
void DNSResolver::setCacheMaxSize(int entries) { m_cacheMaxSize = entries; }

// -----------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------
void DNSResolver::setDoHEnabled(bool enabled) { m_doHEnabled = enabled; }
bool DNSResolver::isDoHEnabled() const { return m_doHEnabled; }

void DNSResolver::setCustomDnsServer(const QString& server, int port) {
    m_customDnsServer = server;
    m_customDnsPort = port;
}
QString DNSResolver::customDnsServer() const { return m_customDnsServer; }

void DNSResolver::addDoHProvider(const DoHProvider& provider) {
    m_doHProviders << provider;
}
void DNSResolver::setActiveDoHProvider(const QString& name) {
    for (int i = 0; i < m_doHProviders.size(); ++i) {
        if (m_doHProviders[i].name == name) {
            m_activeProviderIndex = i;
            return;
        }
    }
}

void DNSResolver::setDnssecRequired(bool required) { m_dnssecRequired = required; }
bool DNSResolver::isDnssecRequired() const { return m_dnssecRequired; }

// -----------------------------------------------------------------------
// Threat database
// -----------------------------------------------------------------------
void DNSResolver::addMaliciousDomain(const QString& domain) {
    m_maliciousSet.insert(domain.toLower());
    m_maliciousDomains[domain.toLower()] = QDateTime::currentDateTimeUtc();
}

void DNSResolver::loadMaliciousDomainsFromFile(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        addMaliciousDomain(line.split('\t').first());
    }
}

bool DNSResolver::isMaliciousDomain(const QString& domain) const {
    return m_maliciousSet.contains(domain.toLower());
}

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------
QString DNSResolver::recordTypeName(DnsRecordType type) const {
    switch (type) {
        case DnsRecordType::A:     return "A";
        case DnsRecordType::AAAA:  return "AAAA";
        case DnsRecordType::CNAME: return "CNAME";
        case DnsRecordType::MX:    return "MX";
        case DnsRecordType::TXT:   return "TXT";
        case DnsRecordType::NS:    return "NS";
        case DnsRecordType::SOA:   return "SOA";
    }
    return "A";
}

int DNSResolver::recordTypeNumber(DnsRecordType type) const {
    switch (type) {
        case DnsRecordType::A:     return 1;
        case DnsRecordType::NS:    return 2;
        case DnsRecordType::CNAME: return 5;
        case DnsRecordType::SOA:   return 6;
        case DnsRecordType::MX:    return 15;
        case DnsRecordType::TXT:   return 16;
        case DnsRecordType::AAAA:  return 28;
    }
    return 1;
}

} // namespace Network
} // namespace Ordinal
