#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QHash>
#include <QStringList>
#include <QRegularExpression>
#include <QJsonObject>
#include <QDateTime>
#include <QSet>

namespace Ordinal {
namespace Privacy {

// Cookie consent banner auto-handler
// Automatically dismisses/rejects cookie consent popups

struct ConsentAction {
    enum class Type { Reject, AcceptNecessary, AcceptAll, Dismiss, Custom };
    Type type = Type::Reject;
    QString selector;           // CSS selector or XPath for the button
    QString jsCode;             // optional JS to execute
    bool confirmed = false;     // did this action succeed?
    QDateTime timestamp;
};

struct ConsentBannerInfo {
    QUrl pageUrl;
    QString bannerType;         // "CookieBot", "OneTrust", "Quantcast", "Osano", "Generic"
    bool detected = false;
    bool handled = false;
    ConsentAction action;
    QDateTime detectedAt;
};

// Known consent management platforms (CMPs) and their selectors
struct CmpPattern {
    QString name;
    QStringList detectSelectors;      // CSS selectors to detect the banner
    QStringList rejectSelectors;      // CSS selectors for reject/decline buttons
    QStringList acceptNecessarySelectors;  // selectors for "accept necessary only"
    QString rejectJs;                 // JS fallback to reject
};

class ConsentHandler : public QObject {
    Q_OBJECT
public:
    explicit ConsentHandler(QObject* parent = nullptr);
    ~ConsentHandler() override;

    // Main interface
    bool isEnabled() const;
    void setEnabled(bool enabled);

    // Policy: what to do when a consent banner is found
    enum class Policy {
        RejectAll,          // Reject all non-essential cookies
        AcceptNecessary,    // Accept only necessary cookies
        AcceptAll,          // Accept everything (user prefers convenience)
        Ask,                // Notify user and let them decide
        Ignore              // Don't touch banners
    };
    Policy policy() const;
    void setPolicy(Policy policy);

    // Scan page HTML/DOM for consent banners
    ConsentBannerInfo detectBanner(const QString& html, const QUrl& url) const;

    // Generate JS to auto-handle the detected banner
    QString generateHandlerJs(const ConsentBannerInfo& info) const;

    // Manual interaction
    void handleBanner(const ConsentBannerInfo& info);

    // Stats
    int totalHandled() const;
    int totalDetected() const;
    QList<ConsentBannerInfo> recentBanners(int limit = 20) const;

    // Whitelist: sites where we don't auto-handle
    void addWhitelist(const QString& domain);
    void removeWhitelist(const QString& domain);
    bool isWhitelisted(const QString& domain) const;
    QStringList whitelist() const;

    // CMP patterns
    QList<CmpPattern> knownCmps() const;
    void addCmpPattern(const CmpPattern& pattern);

signals:
    void bannerDetected(const ConsentBannerInfo& info);
    void bannerHandled(const ConsentBannerInfo& info);
    void policyChanged(Policy policy);
    void askUser(const ConsentBannerInfo& info);

private:
    void initKnownCmps();
    CmpPattern matchCmp(const QString& html) const;
    QStringList findRejectButtons(const QString& html, const CmpPattern& cmp) const;

    bool m_enabled = true;
    Policy m_policy = Policy::RejectAll;

    QList<CmpPattern> m_cmps;
    QList<ConsentBannerInfo> m_history;
    QSet<QString> m_whitelist;

    int m_totalDetected = 0;
    int m_totalHandled = 0;
};

} // namespace Privacy
} // namespace Ordinal
