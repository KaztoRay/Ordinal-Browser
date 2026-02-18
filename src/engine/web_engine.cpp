#include "web_engine.h"
#include <QWebEngineCookieStore>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QWebEngineScriptCollection>
#include <QScreen>
#include <QApplication>
#include <QBuffer>
#include <QPixmap>
#include <iostream>

namespace Ordinal {
namespace Engine {

// ============================================================
// OrdinalWebPage
// ============================================================

OrdinalWebPage::OrdinalWebPage(QWebEngineProfile* profile, QObject* parent)
    : QWebEnginePage(profile, parent)
{
    injectSecurityScripts();
}

OrdinalWebPage::~OrdinalWebPage() = default;

OrdinalWebPage::SecurityLevel OrdinalWebPage::securityLevel() const
{
    return m_securityLevel;
}

void OrdinalWebPage::injectSecurityScripts()
{
    // WebRTC 보호 스크립트
    QWebEngineScript webrtcGuard;
    webrtcGuard.setName("ordinal-webrtc-guard");
    webrtcGuard.setSourceCode(QStringLiteral(R"(
(function() {
    'use strict';
    const OrigRTC = window.RTCPeerConnection || window.webkitRTCPeerConnection;
    if (!OrigRTC) return;
    window.RTCPeerConnection = function(config, constraints) {
        config = config || {};
        config.iceTransportPolicy = 'relay';
        if (config.iceServers) {
            config.iceServers = config.iceServers.filter(s => {
                const urls = Array.isArray(s.urls) ? s.urls : [s.urls || s.url];
                return urls.some(u => u && u.startsWith('turn:'));
            });
        }
        return new OrigRTC(config, constraints);
    };
    window.RTCPeerConnection.prototype = OrigRTC.prototype;
    if (window.webkitRTCPeerConnection) window.webkitRTCPeerConnection = window.RTCPeerConnection;
})();
    )"));
    webrtcGuard.setInjectionPoint(QWebEngineScript::DocumentCreation);
    webrtcGuard.setWorldId(QWebEngineScript::ApplicationWorld);
    webrtcGuard.setRunsOnSubFrames(true);
    scripts().insert(webrtcGuard);

    // 핑거프린팅 방지 스크립트
    QWebEngineScript antiFingerprint;
    antiFingerprint.setName("ordinal-anti-fingerprint");
    antiFingerprint.setSourceCode(QStringLiteral(R"(
(function() {
    'use strict';
    // Canvas fingerprinting 방지
    const origToDataURL = HTMLCanvasElement.prototype.toDataURL;
    HTMLCanvasElement.prototype.toDataURL = function(type) {
        const ctx = this.getContext('2d');
        if (ctx) {
            const imgData = ctx.getImageData(0, 0, this.width, this.height);
            for (let i = 0; i < imgData.data.length; i += 4) {
                imgData.data[i] ^= 1;  // 미세한 노이즈 추가
            }
            ctx.putImageData(imgData, 0, 0);
        }
        return origToDataURL.apply(this, arguments);
    };

    // AudioContext fingerprinting 방지
    if (window.AudioContext || window.webkitAudioContext) {
        const OrigAC = window.AudioContext || window.webkitAudioContext;
        const origCreateOscillator = OrigAC.prototype.createOscillator;
        OrigAC.prototype.createOscillator = function() {
            const osc = origCreateOscillator.apply(this, arguments);
            const origConnect = osc.connect.bind(osc);
            osc.connect = function(dest) {
                if (dest instanceof AnalyserNode) {
                    // Noise 추가
                }
                return origConnect(dest);
            };
            return osc;
        };
    }

    // Navigator 속성 노이즈
    Object.defineProperty(navigator, 'hardwareConcurrency', {
        get: () => 4  // 일반적인 값으로 고정
    });
})();
    )"));
    antiFingerprint.setInjectionPoint(QWebEngineScript::DocumentCreation);
    antiFingerprint.setWorldId(QWebEngineScript::ApplicationWorld);
    antiFingerprint.setRunsOnSubFrames(true);
    scripts().insert(antiFingerprint);
}

// Qt6.5+ certificateError is handled via signal, not override

void OrdinalWebPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
    const QString& message, int lineNumber, const QString& sourceID)
{
    emit consoleMessage(static_cast<int>(level), message, lineNumber, sourceID);
}

bool OrdinalWebPage::acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame)
{
    Q_UNUSED(type)
    Q_UNUSED(isMainFrame)

    // HTTPS 여부로 보안 레벨 결정
    if (url.scheme() == "https") {
        m_securityLevel = SecurityLevel::Safe;
    } else if (url.scheme() == "http") {
        m_securityLevel = SecurityLevel::Warning;
    } else if (url.scheme() == "file" || url.scheme() == "ordinal") {
        m_securityLevel = SecurityLevel::Safe;
    } else {
        m_securityLevel = SecurityLevel::Unknown;
    }
    emit securityLevelChanged(m_securityLevel);

    return true;
}

QWebEnginePage* OrdinalWebPage::createWindow(WebWindowType type)
{
    Q_UNUSED(type)
    // 새 창 요청은 새 탭으로 처리 (MainWindow에서 핸들링)
    return nullptr;
}

// ============================================================
// OrdinalWebView
// ============================================================

OrdinalWebView::OrdinalWebView(QWebEngineProfile* profile, QWidget* parent)
    : QWebEngineView(parent)
{
    m_page = new OrdinalWebPage(profile, this);
    setPage(m_page);
    setupConnections();

    // 기본 설정
    settings()->setAttribute(QWebEngineSettings::PluginsEnabled, true);
    settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings()->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    settings()->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    settings()->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, false);
    settings()->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);
    settings()->setAttribute(QWebEngineSettings::PdfViewerEnabled, true);
    settings()->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, true);
}

OrdinalWebView::~OrdinalWebView() = default;

void OrdinalWebView::navigate(const QUrl& url)
{
    load(url);
}

void OrdinalWebView::navigate(const QString& urlOrSearch)
{
    QUrl resolved = resolveInput(urlOrSearch);
    load(resolved);
}

void OrdinalWebView::goBack() { back(); }
void OrdinalWebView::goForward() { forward(); }

void OrdinalWebView::reload()
{
    QWebEngineView::reload();
}

void OrdinalWebView::stopLoading()
{
    stop();
}

QString OrdinalWebView::currentTitle() const { return title(); }
QUrl OrdinalWebView::currentUrl() const { return url(); }
QIcon OrdinalWebView::currentIcon() const { return icon(); }
bool OrdinalWebView::isLoading() const { return m_loading; }
int OrdinalWebView::loadProgress() const { return m_loadProgress; }

void OrdinalWebView::findText(const QString& text, bool forward, bool caseSensitive)
{
    QWebEnginePage::FindFlags flags;
    if (!forward) flags |= QWebEnginePage::FindBackward;
    if (caseSensitive) flags |= QWebEnginePage::FindCaseSensitively;
    page()->findText(text, flags);
}

void OrdinalWebView::clearFind()
{
    page()->findText(QString());
}

void OrdinalWebView::zoomIn()
{
    setZoomFactor(QWebEngineView::zoomFactor() * 1.1);
}

void OrdinalWebView::zoomOut()
{
    setZoomFactor(QWebEngineView::zoomFactor() / 1.1);
}

void OrdinalWebView::resetZoom()
{
    setZoomFactor(1.0);
}

double OrdinalWebView::zoomFactor() const
{
    return QWebEngineView::zoomFactor();
}

void OrdinalWebView::viewSource()
{
    QUrl sourceUrl = QUrl("view-source:" + url().toString());
    emit newTabRequested(sourceUrl);
}

void OrdinalWebView::getPageText(std::function<void(const QString&)> callback)
{
    page()->toPlainText([callback](const QString& text) {
        callback(text);
    });
}

void OrdinalWebView::getPageHtml(std::function<void(const QString&)> callback)
{
    page()->toHtml([callback](const QString& html) {
        callback(html);
    });
}

QPixmap OrdinalWebView::captureScreenshot()
{
    return grab();
}

void OrdinalWebView::openDevTools()
{
    // DevTools는 별도 창으로 열기
    auto* devPage = new QWebEnginePage(page()->profile());
    page()->setDevToolsPage(devPage);
    auto* devView = new QWebEngineView();
    devView->setPage(devPage);
    devView->setWindowTitle("Ordinal DevTools — " + title());
    devView->resize(1200, 800);
    devView->show();
}

OrdinalWebPage::SecurityLevel OrdinalWebView::securityLevel() const
{
    return m_page ? m_page->securityLevel() : OrdinalWebPage::SecurityLevel::Unknown;
}

OrdinalWebPage* OrdinalWebView::ordinalPage() const
{
    return m_page;
}

void OrdinalWebView::setAudioMuted(bool muted)
{
    page()->setAudioMuted(muted);
}

bool OrdinalWebView::isAudioMuted() const
{
    return page()->isAudioMuted();
}

void OrdinalWebView::setupConnections()
{
    connect(this, &QWebEngineView::titleChanged, this, &OrdinalWebView::pageTitleChanged);
    connect(this, &QWebEngineView::urlChanged, this, &OrdinalWebView::pageUrlChanged);
    connect(this, &QWebEngineView::iconChanged, this, &OrdinalWebView::pageIconChanged);

    connect(this, &QWebEngineView::loadStarted, this, [this]() {
        m_loading = true;
        m_loadProgress = 0;
        emit pageLoadStarted();
    });

    connect(this, &QWebEngineView::loadProgress, this, [this](int progress) {
        m_loadProgress = progress;
        emit pageLoadProgress(progress);
    });

    connect(this, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_loading = false;
        m_loadProgress = ok ? 100 : 0;
        emit pageLoadFinished(ok);
    });

    connect(m_page, &OrdinalWebPage::securityLevelChanged,
            this, &OrdinalWebView::securityLevelChanged);
}

QUrl OrdinalWebView::resolveInput(const QString& input) const
{
    QString trimmed = input.trimmed();

    // 이미 URL인 경우
    QUrl url(trimmed);
    if (url.isValid() && !url.scheme().isEmpty()) {
        return url;
    }

    // 도메인 패턴 체크 (xxx.xxx)
    static QRegularExpression domainRe(R"(^[\w.-]+\.\w{2,}(/.*)?$)");
    if (domainRe.match(trimmed).hasMatch()) {
        return QUrl("https://" + trimmed);
    }

    // localhost 체크
    if (trimmed.startsWith("localhost") || trimmed.startsWith("127.0.0.1")) {
        return QUrl("http://" + trimmed);
    }

    // 검색 쿼리로 처리 (DuckDuckGo — 프라이버시 기본)
    QString encoded = QUrl::toPercentEncoding(trimmed);
    return QUrl("https://duckduckgo.com/?q=" + encoded);
}

// ============================================================
// AdBlockInterceptor
// ============================================================

AdBlockInterceptor::AdBlockInterceptor(QObject* parent)
    : QWebEngineUrlRequestInterceptor(parent)
{
    loadDefaultFilters();
}

void AdBlockInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info)
{
    if (!m_enabled) return;

    QUrl url = info.requestUrl();
    QUrl firstParty = info.firstPartyUrl();
    QString domain = url.host().toLower();

    // 화이트리스트 체크
    if (isWhitelisted(firstParty.host())) return;

    // 알려진 광고/추적 도메인 빠른 차단
    for (const auto& adDomain : m_adDomains) {
        if (domain == adDomain || domain.endsWith("." + adDomain)) {
            info.block(true);
            m_blockedCount.fetch_add(1);
            emit requestBlocked(url, "Known ad/tracking domain: " + adDomain);
            return;
        }
    }

    // 필터 룰 매칭
    if (matchesFilter(url, firstParty, static_cast<int>(info.resourceType()))) {
        info.block(true);
        m_blockedCount.fetch_add(1);
        emit requestBlocked(url, "Filter rule match");
    }
}

int AdBlockInterceptor::blockedCount() const { return m_blockedCount.load(); }
void AdBlockInterceptor::resetStats() { m_blockedCount.store(0); }

void AdBlockInterceptor::addWhitelist(const QString& domain) { m_whitelist.insert(domain.toLower()); }
void AdBlockInterceptor::removeWhitelist(const QString& domain) { m_whitelist.remove(domain.toLower()); }
bool AdBlockInterceptor::isWhitelisted(const QString& domain) const {
    QString d = domain.toLower();
    return m_whitelist.contains(d) || m_whitelist.contains(d.replace("www.", ""));
}

void AdBlockInterceptor::setEnabled(bool enabled) { m_enabled = enabled; }
bool AdBlockInterceptor::isEnabled() const { return m_enabled; }

void AdBlockInterceptor::loadFilterList(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    while (!file.atEnd()) {
        QString line = file.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('!') || line.startsWith('[')) continue;

        FilterRule rule;
        rule.isException = line.startsWith("@@");
        if (rule.isException) line = line.mid(2);

        // 간단한 도메인 룰 파싱
        if (line.startsWith("||") && line.endsWith("^")) {
            QString domain = line.mid(2, line.length() - 3);
            if (!domain.contains('/') && !domain.contains('*')) {
                m_adDomains.append(domain);
                continue;
            }
        }

        rule.pattern = line;
        m_rules.append(rule);
    }
}

void AdBlockInterceptor::loadDefaultFilters()
{
    // 가장 흔한 광고/추적 도메인 내장
    m_adDomains = {
        "doubleclick.net", "googlesyndication.com", "googleadservices.com",
        "google-analytics.com", "googletagmanager.com", "googletagservices.com",
        "adnxs.com", "adsrvr.org", "adform.net", "serving-sys.com",
        "facebook.net", "fbcdn.net",
        "analytics.twitter.com", "ads-twitter.com",
        "amazon-adsystem.com", "ads.yahoo.com",
        "taboola.com", "outbrain.com", "mgid.com", "revcontent.com",
        "criteo.com", "criteo.net", "casalemedia.com",
        "rubiconproject.com", "pubmatic.com", "openx.net",
        "moatads.com", "scorecardresearch.com", "quantserve.com",
        "chartbeat.com", "hotjar.com", "mouseflow.com",
        "mixpanel.com", "segment.com", "amplitude.com",
        "newrelic.com", "bugsnag.com", "sentry.io",
        "popads.net", "popcash.net", "propellerads.com",
        "exoclick.com", "juicyads.com", "trafficjunky.com",
        "ad.doubleclick.net", "pagead2.googlesyndication.com",
        "securepubads.g.doubleclick.net",
        "tracking.example.com"
    };
}

bool AdBlockInterceptor::matchesFilter(const QUrl& url, const QUrl& firstPartyUrl, int resourceType) const
{
    Q_UNUSED(firstPartyUrl)
    Q_UNUSED(resourceType)

    QString urlStr = url.toString().toLower();

    // 패턴 매칭 (간단 버전)
    for (const auto& rule : m_rules) {
        if (rule.isException) continue;

        if (rule.isRegex) {
            QRegularExpression re(rule.pattern, QRegularExpression::CaseInsensitiveOption);
            if (re.match(urlStr).hasMatch()) return true;
        } else {
            // 간단한 와일드카드 매칭
            if (urlStr.contains(rule.pattern.toLower())) return true;
        }
    }
    return false;
}

bool AdBlockInterceptor::matchesDomain(const QString& domain, const QStringList& patterns) const
{
    for (const auto& pattern : patterns) {
        if (domain == pattern || domain.endsWith("." + pattern)) return true;
    }
    return false;
}

// ============================================================
// SecurityInterceptor
// ============================================================

SecurityInterceptor::SecurityInterceptor(QObject* parent)
    : QWebEngineUrlRequestInterceptor(parent)
{
    // 알려진 악성 도메인 내장 (샘플)
    m_minerDomains = {
        "coinhive.com", "coin-hive.com", "jsecoin.com",
        "cryptoloot.pro", "crypto-loot.com", "coinpot.co",
        "coinhive.min.js", "minero.cc", "webminepool.com"
    };
}

void SecurityInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info)
{
    if (!m_enabled) return;

    QUrl url = info.requestUrl();

    if (isCryptoMiner(url)) {
        info.block(true);
        emit threatDetected(url, "cryptominer");
        return;
    }

    if (isKnownMalicious(url)) {
        info.block(true);
        emit threatDetected(url, "malware");
        return;
    }
}

void SecurityInterceptor::setEnabled(bool enabled) { m_enabled = enabled; }
bool SecurityInterceptor::isEnabled() const { return m_enabled; }

bool SecurityInterceptor::isKnownMalicious(const QUrl& url) const
{
    QString domain = url.host().toLower();
    return m_maliciousDomains.contains(domain);
}

bool SecurityInterceptor::isPhishing(const QUrl& url) const
{
    QString domain = url.host().toLower();
    return m_phishingDomains.contains(domain);
}

bool SecurityInterceptor::isCryptoMiner(const QUrl& url) const
{
    QString host = url.host().toLower();
    QString path = url.path().toLower();

    for (const auto& miner : m_minerDomains) {
        if (host.contains(miner) || path.contains(miner)) return true;
    }
    return false;
}

// ============================================================
// OrdinalProfile
// ============================================================

OrdinalProfile::OrdinalProfile(const QString& storagePath, QObject* parent)
    : QObject(parent)
    , m_storagePath(storagePath)
{
    m_profile = new QWebEngineProfile("Ordinal", this);
    setupProfile();
    setupInterceptors();
}

OrdinalProfile::~OrdinalProfile() = default;

QWebEngineProfile* OrdinalProfile::profile() const { return m_profile; }
AdBlockInterceptor* OrdinalProfile::adBlocker() const { return m_adBlocker; }
SecurityInterceptor* OrdinalProfile::securityInterceptor() const { return m_securityInterceptor; }

void OrdinalProfile::setDoNotTrack(bool enabled)
{
    m_profile->setHttpAcceptLanguage(enabled ? "en-US,en;q=0.9" : "");
    // Qt6 에서는 DNT 헤더를 직접 설정하는 API 없음, 인터셉터로 추가
}

void OrdinalProfile::setJavaScriptEnabled(bool enabled)
{
    m_profile->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, enabled);
}

void OrdinalProfile::setWebRtcPolicy(int policy)
{
    Q_UNUSED(policy)
    m_profile->settings()->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);
}

void OrdinalProfile::setUserAgent(const QString& ua)
{
    m_profile->setHttpUserAgent(ua);
}

void OrdinalProfile::clearBrowsingData()
{
    m_profile->clearHttpCache();
    clearCookies();
}

void OrdinalProfile::setDownloadPath(const QString& path)
{
    m_profile->setDownloadPath(path);
}

void OrdinalProfile::clearCookies()
{
    m_profile->cookieStore()->deleteAllCookies();
}

void OrdinalProfile::blockThirdPartyCookies(bool block)
{
    Q_UNUSED(block)
    // Qt6 WebEngine은 기본적으로 third-party 쿠키를 허용
    // 커스텀 쿠키 필터 사용
    if (block) {
        m_profile->cookieStore()->setCookieFilter([](const QWebEngineCookieStore::FilterRequest& request) {
            return !request.thirdParty;  // third-party 쿠키 차단
        });
    } else {
        m_profile->cookieStore()->setCookieFilter(nullptr);
    }
}

void OrdinalProfile::setupProfile()
{
    // 저장 경로
    if (!m_storagePath.isEmpty()) {
        m_profile->setPersistentStoragePath(m_storagePath + "/storage");
        m_profile->setCachePath(m_storagePath + "/cache");
        m_profile->setDownloadPath(
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    }

    // User Agent 커스터마이징
    QString defaultUA = m_profile->httpUserAgent();
    // Chrome UA와 유사하게 (호환성)
    m_profile->setHttpUserAgent(
        defaultUA.replace("QtWebEngine", "OrdinalBrowser/1.2.0"));

    // 기본 보안 설정
    auto* settings = m_profile->settings();
    settings->setAttribute(QWebEngineSettings::AutoLoadImages, true);
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
    settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    settings->setAttribute(QWebEngineSettings::ScrollAnimatorEnabled, true);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, true);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    settings->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, false);
    settings->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);
    settings->setAttribute(QWebEngineSettings::PdfViewerEnabled, true);
    settings->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, true);
    settings->setAttribute(QWebEngineSettings::NavigateOnDropEnabled, false);

    // Third-party 쿠키 차단
    blockThirdPartyCookies(true);

    // 다운로드 시그널
    connect(m_profile, &QWebEngineProfile::downloadRequested,
            this, &OrdinalProfile::downloadRequested);
}

void OrdinalProfile::setupInterceptors()
{
    // 광고 차단 인터셉터
    m_adBlocker = new AdBlockInterceptor(this);

    // 보안 인터셉터
    m_securityInterceptor = new SecurityInterceptor(this);

    // 체인 순서: 보안 → 광고 차단
    // Qt6은 단일 인터셉터만 지원, 래핑 필요
    // 여기서는 광고 차단을 기본으로 설정
    m_profile->setUrlRequestInterceptor(m_adBlocker);
}

void OrdinalProfile::loadDefaultAdBlockRules()
{
    m_adBlocker->loadDefaultFilters();
}

} // namespace Engine
} // namespace Ordinal
