#include "webrtc_guard.h"
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QUuid>
#include <QJsonArray>
#include <QDateTime>

namespace Ordinal {
namespace Privacy {

WebRtcGuard::WebRtcGuard(QObject* parent)
    : QObject(parent)
{
    // Block well-known public STUN servers by default
    m_blockedStun = {
        "stun.l.google.com:19302",
        "stun1.l.google.com:19302",
        "stun2.l.google.com:19302",
        "stun3.l.google.com:19302",
        "stun4.l.google.com:19302",
        "stun.stunprotocol.org:3478"
    };
}

WebRtcGuard::~WebRtcGuard() = default;

WebRtcPolicy WebRtcGuard::policy() const { return m_policy; }

void WebRtcGuard::setPolicy(WebRtcPolicy policy)
{
    if (m_policy == policy) return;
    m_policy = policy;
    emit policyChanged(policy);
}

bool WebRtcGuard::isEnabled() const { return m_enabled; }
void WebRtcGuard::setEnabled(bool enabled) { m_enabled = enabled; }

// --------------- JS Enforcement ---------------

QString WebRtcGuard::generateEnforcementJs() const
{
    if (!m_enabled) return {};

    switch (m_policy) {
    case WebRtcPolicy::Disabled:
        return QStringLiteral(R"(
(function() {
    'use strict';
    // Completely disable WebRTC APIs
    const noop = function() { throw new DOMException('WebRTC is disabled by OrdinalV8', 'NotAllowedError'); };
    Object.defineProperty(window, 'RTCPeerConnection', { value: undefined, writable: false, configurable: false });
    Object.defineProperty(window, 'webkitRTCPeerConnection', { value: undefined, writable: false, configurable: false });
    Object.defineProperty(window, 'mozRTCPeerConnection', { value: undefined, writable: false, configurable: false });
    Object.defineProperty(navigator, 'getUserMedia', { value: noop, writable: false });
    Object.defineProperty(navigator.mediaDevices || {}, 'getUserMedia', { value: noop, writable: false });
    console.log('[Ordinal] WebRTC completely disabled');
})();
)");

    case WebRtcPolicy::ForceRelay:
        return QStringLiteral(R"(
(function() {
    'use strict';
    const OriginalRTC = window.RTCPeerConnection || window.webkitRTCPeerConnection;
    if (!OriginalRTC) return;

    window.RTCPeerConnection = function(config, constraints) {
        config = config || {};
        // Force relay-only ICE transport policy
        config.iceTransportPolicy = 'relay';
        // Remove all STUN servers, keep only TURN
        if (config.iceServers) {
            config.iceServers = config.iceServers.filter(server => {
                const urls = Array.isArray(server.urls) ? server.urls : [server.urls || server.url];
                return urls.some(u => u && u.startsWith('turn:'));
            });
        }
        console.log('[Ordinal] WebRTC forced to relay-only mode');
        return new OriginalRTC(config, constraints);
    };
    window.RTCPeerConnection.prototype = OriginalRTC.prototype;
    if (window.webkitRTCPeerConnection) {
        window.webkitRTCPeerConnection = window.RTCPeerConnection;
    }
})();
)");

    case WebRtcPolicy::PublicOnly:
        return QStringLiteral(R"(
(function() {
    'use strict';
    const OriginalRTC = window.RTCPeerConnection || window.webkitRTCPeerConnection;
    if (!OriginalRTC) return;

    window.RTCPeerConnection = function(config, constraints) {
        const pc = new OriginalRTC(config, constraints);
        const origAddEvent = pc.addEventListener.bind(pc);
        const origOnIce = Object.getOwnPropertyDescriptor(OriginalRTC.prototype, 'onicecandidate');

        // Filter ICE candidates to remove private IPs
        const filterCandidate = function(event) {
            if (event.candidate && event.candidate.candidate) {
                const c = event.candidate.candidate;
                // Block RFC 1918 private IPs
                if (/(?:^|\s)(?:10\.|172\.(?:1[6-9]|2\d|3[01])\.|192\.168\.)/.test(c)) {
                    console.log('[Ordinal] Blocked private IP in ICE candidate');
                    return null;
                }
            }
            return event;
        };

        pc.addEventListener = function(type, listener, options) {
            if (type === 'icecandidate') {
                const wrapped = function(event) {
                    const filtered = filterCandidate(event);
                    if (filtered) listener(filtered);
                };
                return origAddEvent(type, wrapped, options);
            }
            return origAddEvent(type, listener, options);
        };
        return pc;
    };
    window.RTCPeerConnection.prototype = OriginalRTC.prototype;
    console.log('[Ordinal] WebRTC private IP filtering active');
})();
)");

    case WebRtcPolicy::DisableNonProxied:
        return QStringLiteral(R"(
(function() {
    'use strict';
    const OriginalRTC = window.RTCPeerConnection || window.webkitRTCPeerConnection;
    if (!OriginalRTC) return;

    window.RTCPeerConnection = function(config, constraints) {
        config = config || {};
        // Use default_public_interface_only for mDNS-based privacy
        config.iceTransportPolicy = config.iceTransportPolicy || 'all';
        const pc = new OriginalRTC(config, constraints);

        const origSetLocal = pc.setLocalDescription.bind(pc);
        pc.setLocalDescription = function(desc) {
            if (desc && desc.sdp) {
                // Remove private IP candidates from SDP
                desc.sdp = desc.sdp.replace(/a=candidate:.*(?:10\.|172\.(?:1[6-9]|2\d|3[01])\.|192\.168\.)\S+.*\r?\n/g, '');
            }
            return origSetLocal(desc);
        };
        console.log('[Ordinal] WebRTC non-proxied connections disabled');
        return pc;
    };
    window.RTCPeerConnection.prototype = OriginalRTC.prototype;
})();
)");

    case WebRtcPolicy::Default:
    default:
        return {};
    }
}

QJsonObject WebRtcGuard::chromeRtcPolicy() const
{
    QJsonObject policy;
    switch (m_policy) {
    case WebRtcPolicy::Disabled:
        policy["webRTCIPHandlingPolicy"] = "disable_non_proxied_udp";
        policy["webRTCEnabled"] = false;
        break;
    case WebRtcPolicy::ForceRelay:
        policy["webRTCIPHandlingPolicy"] = "disable_non_proxied_udp";
        policy["iceTransportPolicy"] = "relay";
        break;
    case WebRtcPolicy::PublicOnly:
        policy["webRTCIPHandlingPolicy"] = "default_public_interface_only";
        break;
    case WebRtcPolicy::DisableNonProxied:
        policy["webRTCIPHandlingPolicy"] = "disable_non_proxied_udp";
        break;
    case WebRtcPolicy::Default:
    default:
        policy["webRTCIPHandlingPolicy"] = "default";
        break;
    }
    return policy;
}

// --------------- Leak check ---------------

WebRtcLeakReport WebRtcGuard::checkForLeaks() const
{
    WebRtcLeakReport report;
    report.checkedAt = QDateTime::currentDateTimeUtc();

    auto interfaces = enumerateInterfaces();
    for (const auto& iface : interfaces) {
        if (iface.isLoopback) continue;

        if (iface.isPrivate) {
            report.localIps.append(iface.address.toString());
        } else {
            report.publicIps.append(iface.address.toString());
        }

        if (iface.isVpn) {
            report.vpnDetected = true;
            report.vpnInterface = iface.name;
        }
    }

    // If VPN is detected but we have non-VPN public IPs, that's a potential leak
    if (report.vpnDetected && report.publicIps.size() > 1) {
        report.leakDetected = true;
    }

    // If we have local IPs exposed and policy should prevent that
    if (!report.localIps.isEmpty() &&
        (m_policy == WebRtcPolicy::PublicOnly ||
         m_policy == WebRtcPolicy::ForceRelay ||
         m_policy == WebRtcPolicy::Disabled)) {
        report.leakDetected = true;
    }

    if (report.leakDetected) {
        emit const_cast<WebRtcGuard*>(this)->leakDetected(report);
    }

    return report;
}

// --------------- Site overrides ---------------

void WebRtcGuard::addSiteOverride(const QString& domain, WebRtcPolicy policy)
{
    m_siteOverrides[domain.toLower()] = policy;
    emit siteOverrideChanged(domain, policy);
}

void WebRtcGuard::removeSiteOverride(const QString& domain)
{
    m_siteOverrides.remove(domain.toLower());
}

WebRtcPolicy WebRtcGuard::sitePolicy(const QString& domain) const
{
    QString d = domain.toLower();
    if (m_siteOverrides.contains(d)) return m_siteOverrides[d];
    // Check without www.
    d.replace("www.", "");
    if (m_siteOverrides.contains(d)) return m_siteOverrides[d];
    return m_policy;
}

QHash<QString, WebRtcPolicy> WebRtcGuard::siteOverrides() const
{
    return m_siteOverrides;
}

// --------------- STUN blocking ---------------

void WebRtcGuard::blockStunServer(const QString& server)
{
    m_blockedStun.insert(server.toLower());
}

void WebRtcGuard::unblockStunServer(const QString& server)
{
    m_blockedStun.remove(server.toLower());
}

bool WebRtcGuard::isStunBlocked(const QString& server) const
{
    return m_blockedStun.contains(server.toLower());
}

QStringList WebRtcGuard::blockedStunServers() const
{
    return QStringList(m_blockedStun.begin(), m_blockedStun.end());
}

// --------------- ICE candidate filtering ---------------

QString WebRtcGuard::filterIceCandidate(const QString& candidate) const
{
    if (!m_enabled || m_policy == WebRtcPolicy::Default) return candidate;

    // Parse the candidate line
    // Format: candidate:foundation component transport priority ip port type ...
    static QRegularExpression ipRe(R"((\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}))");
    auto m = ipRe.match(candidate);
    if (!m.hasMatch()) return candidate;

    QHostAddress addr(m.captured(1));

    switch (m_policy) {
    case WebRtcPolicy::Disabled:
        return {};  // Block all candidates

    case WebRtcPolicy::ForceRelay:
        // Only allow relay candidates
        if (!candidate.contains("relay", Qt::CaseInsensitive)) {
            return {};
        }
        return candidate;

    case WebRtcPolicy::PublicOnly:
        if (isPrivateIp(addr)) return {};
        return candidate;

    case WebRtcPolicy::DisableNonProxied:
        if (isPrivateIp(addr)) return {};
        return candidate;

    default:
        return candidate;
    }
}

bool WebRtcGuard::shouldBlockCandidate(const QString& candidate) const
{
    return filterIceCandidate(candidate).isEmpty();
}

// --------------- Network interfaces ---------------

QList<WebRtcGuard::NetworkInterface> WebRtcGuard::enumerateInterfaces() const
{
    QList<NetworkInterface> result;

    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (!(iface.flags() & QNetworkInterface::IsRunning)) continue;

        for (const auto& entry : iface.addressEntries()) {
            QHostAddress addr = entry.ip();
            if (addr.protocol() != QAbstractSocket::IPv4Protocol &&
                addr.protocol() != QAbstractSocket::IPv6Protocol) {
                continue;
            }

            NetworkInterface ni;
            ni.name = iface.name();
            ni.address = addr;
            ni.isLoopback = addr.isLoopback();
            ni.isPrivate = isPrivateIp(addr);
            ni.isVpn = isVpnInterface(iface.name());
            result.append(ni);
        }
    }

    return result;
}

// --------------- Private helpers ---------------

bool WebRtcGuard::isPrivateIp(const QHostAddress& addr) const
{
    if (addr.isLoopback()) return true;

    // Check for IPv4 private ranges
    quint32 ipv4 = addr.toIPv4Address();
    if (ipv4 == 0) return false; // IPv6 or invalid

    // 10.0.0.0/8
    if ((ipv4 & 0xFF000000) == 0x0A000000) return true;
    // 172.16.0.0/12
    if ((ipv4 & 0xFFF00000) == 0xAC100000) return true;
    // 192.168.0.0/16
    if ((ipv4 & 0xFFFF0000) == 0xC0A80000) return true;
    // 169.254.0.0/16 (link-local)
    if ((ipv4 & 0xFFFF0000) == 0xA9FE0000) return true;

    return false;
}

bool WebRtcGuard::isVpnInterface(const QString& name) const
{
    // Common VPN interface names across platforms
    static const QStringList vpnPatterns = {
        "tun", "tap", "utun", "ppp", "wg",   // WireGuard, OpenVPN, etc.
        "nordlynx", "proton", "mullvad",       // Named VPN interfaces
        "ipsec", "vpn", "tailscale"
    };

    QString lower = name.toLower();
    for (const auto& pattern : vpnPatterns) {
        if (lower.contains(pattern)) return true;
    }
    return false;
}

QString WebRtcGuard::obfuscateIp(const QString& ip) const
{
    // Generate a deterministic mDNS-like address from the IP
    QByteArray hash = QUuid::createUuidV5(QUuid(), ip.toUtf8()).toByteArray();
    return QStringLiteral("%1.local").arg(QString(hash.toHex().left(16)));
}

} // namespace Privacy
} // namespace Ordinal
