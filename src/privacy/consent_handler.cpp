#include "consent_handler.h"
#include <QJsonArray>
#include <algorithm>

namespace Ordinal {
namespace Privacy {

ConsentHandler::ConsentHandler(QObject* parent)
    : QObject(parent)
{
    initKnownCmps();
}

ConsentHandler::~ConsentHandler() = default;

bool ConsentHandler::isEnabled() const { return m_enabled; }
void ConsentHandler::setEnabled(bool enabled) { m_enabled = enabled; }

ConsentHandler::Policy ConsentHandler::policy() const { return m_policy; }
void ConsentHandler::setPolicy(Policy policy)
{
    m_policy = policy;
    emit policyChanged(policy);
}

// --------------- Detection ---------------

ConsentBannerInfo ConsentHandler::detectBanner(const QString& html, const QUrl& url) const
{
    ConsentBannerInfo info;
    info.pageUrl = url;
    info.detectedAt = QDateTime::currentDateTimeUtc();

    if (!m_enabled) return info;

    QString domain = url.host().toLower();
    if (m_whitelist.contains(domain) || m_whitelist.contains(domain.replace("www.", ""))) {
        return info;
    }

    // Try to match a known CMP
    CmpPattern matched = matchCmp(html);
    if (!matched.name.isEmpty()) {
        info.detected = true;
        info.bannerType = matched.name;

        ConsentAction action;
        switch (m_policy) {
        case Policy::RejectAll:
        case Policy::AcceptNecessary:
            action.type = ConsentAction::Type::Reject;
            if (!matched.rejectSelectors.isEmpty()) {
                action.selector = matched.rejectSelectors.first();
            } else if (!matched.rejectJs.isEmpty()) {
                action.jsCode = matched.rejectJs;
                action.type = ConsentAction::Type::Custom;
            }
            break;
        case Policy::AcceptAll:
            action.type = ConsentAction::Type::AcceptAll;
            break;
        case Policy::Ask:
        case Policy::Ignore:
            action.type = ConsentAction::Type::Dismiss;
            break;
        }
        action.timestamp = QDateTime::currentDateTimeUtc();
        info.action = action;
        return info;
    }

    // Generic detection: look for common cookie banner patterns
    static QRegularExpression genericBannerRe(
        R"(class="[^"]*(?:cookie-?(?:banner|consent|notice|bar|popup|modal)|gdpr|consent-?(?:banner|modal|overlay)|cc-(?:banner|window))[^"]*")",
        QRegularExpression::CaseInsensitiveOption);

    if (genericBannerRe.match(html).hasMatch()) {
        info.detected = true;
        info.bannerType = "Generic";

        ConsentAction action;
        action.type = ConsentAction::Type::Reject;
        action.timestamp = QDateTime::currentDateTimeUtc();

        // Try to find reject/decline buttons
        static QRegularExpression rejectBtnRe(
            R"(<(?:button|a)[^>]*>([^<]*(?:reject|decline|refuse|deny|거부|필수만|필요한 쿠키만|nur notwendige)[^<]*)</(?:button|a)>)",
            QRegularExpression::CaseInsensitiveOption);
        auto m = rejectBtnRe.match(html);
        if (m.hasMatch()) {
            // Build a generic JS click handler
            action.jsCode = QStringLiteral(
                R"(document.querySelectorAll('button, a').forEach(el => {)"
                R"(  const t = el.textContent.toLowerCase();)"
                R"(  if (t.includes('reject') || t.includes('decline') || )"
                R"(     t.includes('refuse') || t.includes('deny') ||)"
                R"(     t.includes('거부') || t.includes('필수만')) {)"
                R"(    el.click();)"
                R"(  })"
                R"(});)");
            action.type = ConsentAction::Type::Custom;
        }

        // Try dismiss/close if no reject button
        if (action.jsCode.isEmpty()) {
            action.jsCode = QStringLiteral(
                R"(var banner = document.querySelector('[class*="cookie"], [class*="consent"], [class*="gdpr"]');)"
                R"(if (banner) banner.style.display = 'none';)"
                R"(document.querySelectorAll('button, a').forEach(el => {)"
                R"(  const t = el.textContent.toLowerCase();)"
                R"(  if (t.includes('close') || t.includes('×') || t.includes('닫기')) {)"
                R"(    el.click();)"
                R"(  })"
                R"(});)");
            action.type = ConsentAction::Type::Dismiss;
        }

        info.action = action;
    }

    return info;
}

// --------------- JS Generation ---------------

QString ConsentHandler::generateHandlerJs(const ConsentBannerInfo& info) const
{
    if (!info.detected) return {};

    // If we have direct JS, use it
    if (!info.action.jsCode.isEmpty()) {
        return QStringLiteral(
            "(function() { try { %1 } catch(e) { console.log('Ordinal consent handler:', e); } })();")
            .arg(info.action.jsCode);
    }

    // If we have a selector, click it
    if (!info.action.selector.isEmpty()) {
        return QStringLiteral(
            "(function() {"
            "  try {"
            "    var el = document.querySelector('%1');"
            "    if (el) el.click();"
            "  } catch(e) { console.log('Ordinal consent handler:', e); }"
            "})();")
            .arg(info.action.selector);
    }

    return {};
}

void ConsentHandler::handleBanner(const ConsentBannerInfo& info)
{
    if (!info.detected) return;

    m_totalDetected++;

    if (m_policy == Policy::Ask) {
        emit askUser(info);
        return;
    }

    if (m_policy == Policy::Ignore) return;

    // Record the action
    ConsentBannerInfo handled = info;
    handled.handled = true;
    handled.action.confirmed = true;

    m_history.prepend(handled);
    if (m_history.size() > 200) m_history.removeLast();

    m_totalHandled++;
    emit bannerHandled(handled);
}

// --------------- Stats ---------------

int ConsentHandler::totalHandled() const { return m_totalHandled; }
int ConsentHandler::totalDetected() const { return m_totalDetected; }

QList<ConsentBannerInfo> ConsentHandler::recentBanners(int limit) const
{
    return m_history.mid(0, limit);
}

// --------------- Whitelist ---------------

void ConsentHandler::addWhitelist(const QString& domain)
{
    m_whitelist.insert(domain.toLower());
}

void ConsentHandler::removeWhitelist(const QString& domain)
{
    m_whitelist.remove(domain.toLower());
}

bool ConsentHandler::isWhitelisted(const QString& domain) const
{
    QString d = domain.toLower();
    return m_whitelist.contains(d) || m_whitelist.contains(d.replace("www.", ""));
}

QStringList ConsentHandler::whitelist() const
{
    return QStringList(m_whitelist.begin(), m_whitelist.end());
}

// --------------- CMP patterns ---------------

QList<CmpPattern> ConsentHandler::knownCmps() const { return m_cmps; }

void ConsentHandler::addCmpPattern(const CmpPattern& pattern)
{
    m_cmps.append(pattern);
}

// --------------- Private ---------------

void ConsentHandler::initKnownCmps()
{
    // CookieBot
    m_cmps.append({
        "CookieBot",
        {"#CybotCookiebotDialog", "[data-cookieconsent]", "#CybotCookiebotDialogBodyLevelButtonLevelOptinAllowallSelection"},
        {"#CybotCookiebotDialogBodyButtonDecline",
         "#CybotCookiebotDialogBodyLevelButtonLevelOptinDeclineAll",
         "button[data-cookieconsent='decline']"},
        {"#CybotCookiebotDialogBodyLevelButtonLevelOptinAllowallSelection"},
        "Cookiebot.dialog.submitDecline();"
    });

    // OneTrust
    m_cmps.append({
        "OneTrust",
        {"#onetrust-banner-sdk", ".onetrust-pc-dark-filter", "[class*='onetrust']"},
        {"#onetrust-reject-all-handler",
         ".onetrust-close-btn-handler",
         "button.ot-pc-refuse-all-handler"},
        {"#onetrust-accept-btn-handler"}, // necessary is usually reject in OneTrust
        "OneTrust.RejectAll();"
    });

    // Quantcast / TCF
    m_cmps.append({
        "Quantcast",
        {".qc-cmp-ui-container", "#qc-cmp2-container", "[class*='qc-cmp']"},
        {"button[mode='secondary']",
         ".qc-cmp-button[aria-label='DISAGREE']",
         ".qc-cmp2-summary-buttons button:first-child"},
        {},
        R"(window.__tcfapi && window.__tcfapi('addEventListener', 2, function(tcData, success) {
            if (success) window.__tcfapi('setConsent', 2, function(){}, {});
        });)"
    });

    // Osano
    m_cmps.append({
        "Osano",
        {".osano-cm-window", "[class*='osano-cm']"},
        {".osano-cm-deny", "button.osano-cm-denyAll"},
        {".osano-cm-accept-necessary"},
        {}
    });

    // Klaro
    m_cmps.append({
        "Klaro",
        {".klaro", ".cookie-modal", "[class*='klaro']"},
        {".cm-btn-decline", "button.cn-decline"},
        {".cm-btn-accept-necessary"},
        "klaro.getManager().changeAll(false); klaro.getManager().saveAndApplyConsents();"
    });

    // EU Cookie Law (WordPress plugin)
    m_cmps.append({
        "EU Cookie Law",
        {"#cookie-law-info-bar", ".cli-modal", "[class*='cookie-law']"},
        {"#cookie_action_close_header_reject",
         "#wt-cli-reject-btn",
         "a[data-cli_action='reject']"},
        {},
        {}
    });

    // GDPR Cookie Consent (another WP plugin)
    m_cmps.append({
        "GDPR Cookie Consent",
        {"#gdpr-cookie-consent-bar", ".gdpr-consent-modal"},
        {"#cookie_action_reject", ".gdpr_action_button[data-gdpr_action='reject']"},
        {},
        {}
    });

    // Didomi
    m_cmps.append({
        "Didomi",
        {"#didomi-host", "#didomi-notice", "[class*='didomi']"},
        {"#didomi-notice-disagree-button", ".didomi-dismiss-button"},
        {"#didomi-notice-learn-more-button"},
        "Didomi.setUserDisagreeToAll();"
    });

    // TrustArc / TRUSTe
    m_cmps.append({
        "TrustArc",
        {"#truste-consent-track", "#truste-consent-content", "[class*='truste']"},
        {"#truste-consent-required", ".call[onclick*='reject']"},
        {},
        "truste.eu.clickListener('close');"
    });

    // Complianz (WP)
    m_cmps.append({
        "Complianz",
        {"#cmplz-cookiebanner", ".cmplz-cookiebanner", "[class*='cmplz']"},
        {".cmplz-deny", "button.cmplz-btn.cmplz-deny"},
        {".cmplz-save-preferences"},
        {}
    });
}

CmpPattern ConsentHandler::matchCmp(const QString& html) const
{
    for (const auto& cmp : m_cmps) {
        for (const auto& selector : cmp.detectSelectors) {
            // Convert CSS selector to a regex pattern for HTML matching
            QString escaped = QRegularExpression::escape(selector);
            // Check for id selectors
            if (selector.startsWith('#')) {
                QString id = selector.mid(1);
                QRegularExpression re(
                    QStringLiteral(R"(id=["']%1["'])").arg(QRegularExpression::escape(id)),
                    QRegularExpression::CaseInsensitiveOption);
                if (re.match(html).hasMatch()) return cmp;
            }
            // Check for class selectors
            else if (selector.startsWith('.')) {
                QString cls = selector.mid(1);
                QRegularExpression re(
                    QStringLiteral(R"(class="[^"]*%1[^"]*")").arg(QRegularExpression::escape(cls)),
                    QRegularExpression::CaseInsensitiveOption);
                if (re.match(html).hasMatch()) return cmp;
            }
            // Check for attribute selectors [class*='...']
            else if (selector.contains("[class*=")) {
                QRegularExpression extractRe(R"(\[class\*=['"](.*?)['"]\])");
                auto m = extractRe.match(selector);
                if (m.hasMatch()) {
                    if (html.contains(m.captured(1), Qt::CaseInsensitive)) return cmp;
                }
            }
        }
    }
    return {};
}

QStringList ConsentHandler::findRejectButtons(const QString& html, const CmpPattern& cmp) const
{
    QStringList found;
    for (const auto& selector : cmp.rejectSelectors) {
        if (selector.startsWith('#')) {
            QString id = selector.mid(1);
            QRegularExpression re(
                QStringLiteral(R"(id=["']%1["'])").arg(QRegularExpression::escape(id)),
                QRegularExpression::CaseInsensitiveOption);
            if (re.match(html).hasMatch()) {
                found.append(selector);
            }
        } else if (selector.startsWith('.')) {
            QString cls = selector.mid(1);
            QRegularExpression re(
                QStringLiteral(R"(class="[^"]*%1[^"]*")").arg(QRegularExpression::escape(cls)),
                QRegularExpression::CaseInsensitiveOption);
            if (re.match(html).hasMatch()) {
                found.append(selector);
            }
        }
    }
    return found;
}

} // namespace Privacy
} // namespace Ordinal
