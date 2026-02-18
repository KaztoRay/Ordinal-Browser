#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QHostAddress>
#include <QJsonObject>
#include <QSet>

namespace Ordinal {
namespace Privacy {

// WebRTC leak protection
// Prevents WebRTC from leaking real local/public IP addresses
// when using VPN or proxy configurations.

enum class WebRtcPolicy {
    Default,             // Browser default (may leak IPs)
    DisableNonProxied,   // Only use the proxy/VPN interface
    PublicOnly,          // Only expose public IP (no local)
    ForceRelay,          // Force TURN relay (no direct P2P, most private)
    Disabled             // Completely disable WebRTC
};

struct WebRtcLeakReport {
    QStringList localIps;          // RFC 1918 addresses found
    QStringList publicIps;         // public IPs exposed
    QStringList mdnsAddresses;     // mDNS obfuscated addresses
    QStringList stunServers;       // STUN servers contacted
    QStringList turnServers;       // TURN servers contacted
    bool leakDetected = false;
    bool vpnDetected = false;      // is user behind VPN?
    QString vpnInterface;
    QDateTime checkedAt;
};

class WebRtcGuard : public QObject {
    Q_OBJECT
public:
    explicit WebRtcGuard(QObject* parent = nullptr);
    ~WebRtcGuard() override;

    // Policy
    WebRtcPolicy policy() const;
    void setPolicy(WebRtcPolicy policy);

    // Enable/disable
    bool isEnabled() const;
    void setEnabled(bool enabled);

    // Generate V8/Chromium-compatible JS to enforce the policy
    // This is injected into every page before any scripts run
    QString generateEnforcementJs() const;

    // Generate the Chrome-compatible WebRTC policy object
    // (for direct V8 integration)
    QJsonObject chromeRtcPolicy() const;

    // IP enumeration check
    WebRtcLeakReport checkForLeaks() const;

    // Per-site overrides (e.g., allow WebRTC for trusted video call sites)
    void addSiteOverride(const QString& domain, WebRtcPolicy policy);
    void removeSiteOverride(const QString& domain);
    WebRtcPolicy sitePolicy(const QString& domain) const;
    QHash<QString, WebRtcPolicy> siteOverrides() const;

    // STUN/TURN server management
    void blockStunServer(const QString& server);
    void unblockStunServer(const QString& server);
    bool isStunBlocked(const QString& server) const;
    QStringList blockedStunServers() const;

    // ICE candidate filtering
    QString filterIceCandidate(const QString& candidate) const;
    bool shouldBlockCandidate(const QString& candidate) const;

    // Network interface info
    struct NetworkInterface {
        QString name;
        QHostAddress address;
        bool isVpn = false;
        bool isLoopback = false;
        bool isPrivate = false;
    };
    QList<NetworkInterface> enumerateInterfaces() const;

signals:
    void policyChanged(WebRtcPolicy policy);
    void leakDetected(const WebRtcLeakReport& report);
    void candidateBlocked(const QString& candidate);
    void siteOverrideChanged(const QString& domain, WebRtcPolicy policy);

private:
    bool isPrivateIp(const QHostAddress& addr) const;
    bool isVpnInterface(const QString& name) const;
    QString obfuscateIp(const QString& ip) const;

    bool m_enabled = true;
    WebRtcPolicy m_policy = WebRtcPolicy::DisableNonProxied;

    QHash<QString, WebRtcPolicy> m_siteOverrides;
    QSet<QString> m_blockedStun;
};

} // namespace Privacy
} // namespace Ordinal
