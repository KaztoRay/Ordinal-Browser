#pragma once
#include <QObject>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineCertificateError>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlScheme>
#include <QWebEngineHistory>
#include <QWebEngineDownloadRequest>
#include <QUrl>
#include <QString>
#include <QIcon>
#include <QJsonObject>
#include <memory>

namespace Ordinal {
namespace Engine {

class AdBlockInterceptor;
class SecurityInterceptor;

// ============================================================
// OrdinalWebPage — 커스텀 QWebEnginePage
// ============================================================
class OrdinalWebPage : public QWebEnginePage {
    Q_OBJECT
public:
    explicit OrdinalWebPage(QWebEngineProfile* profile, QObject* parent = nullptr);
    ~OrdinalWebPage() override;

    // 보안 레벨
    enum class SecurityLevel { Safe, Warning, Dangerous, Unknown };
    SecurityLevel securityLevel() const;

    // JavaScript 인젝션 (보안 스크립트)
    void injectSecurityScripts();

signals:
    void securityLevelChanged(SecurityLevel level);
    void certificateError(const QUrl& url, const QString& error);
    void consoleMessage(int level, const QString& message, int line, const QString& source);

protected:

    // JS console messages
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString& message, int lineNumber,
                                  const QString& sourceID) override;

    // Navigation control
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override;

    // New window requests (popups, target=_blank)
    QWebEnginePage* createWindow(WebWindowType type) override;

private:
    SecurityLevel m_securityLevel = SecurityLevel::Unknown;
};

// ============================================================
// OrdinalWebView — 탭 하나의 웹 뷰
// ============================================================
class OrdinalWebView : public QWebEngineView {
    Q_OBJECT
public:
    explicit OrdinalWebView(QWebEngineProfile* profile, QWidget* parent = nullptr);
    ~OrdinalWebView() override;

    // 기본 네비게이션
    void navigate(const QUrl& url);
    void navigate(const QString& urlOrSearch);
    void goBack();
    void goForward();
    void reload();
    void stopLoading();

    // 탭 정보
    QString currentTitle() const;
    QUrl currentUrl() const;
    QIcon currentIcon() const;
    bool isLoading() const;
    int loadProgress() const;

    // 검색
    void findText(const QString& text, bool forward = true, bool caseSensitive = false);
    void clearFind();

    // 줌
    void zoomIn();
    void zoomOut();
    void resetZoom();
    double zoomFactor() const;

    // 페이지 소스/텍스트
    void viewSource();
    void getPageText(std::function<void(const QString&)> callback);
    void getPageHtml(std::function<void(const QString&)> callback);

    // 스크린샷
    QPixmap captureScreenshot();

    // DevTools
    void openDevTools();

    // 보안
    OrdinalWebPage::SecurityLevel securityLevel() const;
    OrdinalWebPage* ordinalPage() const;

    // 뮤트
    void setAudioMuted(bool muted);
    bool isAudioMuted() const;

signals:
    void pageTitleChanged(const QString& title);
    void pageUrlChanged(const QUrl& url);
    void pageIconChanged(const QIcon& icon);
    void pageLoadStarted();
    void pageLoadProgress(int progress);
    void pageLoadFinished(bool ok);
    void securityLevelChanged(OrdinalWebPage::SecurityLevel level);
    void newTabRequested(const QUrl& url);
    void closeRequested();

private:
    void setupConnections();
    QUrl resolveInput(const QString& input) const;

    OrdinalWebPage* m_page = nullptr;
    int m_loadProgress = 0;
    bool m_loading = false;
};

// ============================================================
// AdBlockInterceptor — 광고/추적기 차단 인터셉터
// ============================================================
class AdBlockInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit AdBlockInterceptor(QObject* parent = nullptr);

    void interceptRequest(QWebEngineUrlRequestInfo& info) override;

    // 통계
    int blockedCount() const;
    void resetStats();

    // 화이트리스트
    void addWhitelist(const QString& domain);
    void removeWhitelist(const QString& domain);
    bool isWhitelisted(const QString& domain) const;

    // 필터 로드
    void loadFilterList(const QString& filePath);
    void loadDefaultFilters();

    // 토글
    void setEnabled(bool enabled);
    bool isEnabled() const;

signals:
    void requestBlocked(const QUrl& url, const QString& reason);

private:
    bool matchesFilter(const QUrl& url, const QUrl& firstPartyUrl, int resourceType) const;
    bool matchesDomain(const QString& domain, const QStringList& patterns) const;

    struct FilterRule {
        QString pattern;
        QStringList domains;      // 적용 도메인 (빈 = 전체)
        QStringList excludeDomains;
        bool isException = false;
        bool isRegex = false;
        int resourceType = -1;    // -1 = all
    };

    QList<FilterRule> m_rules;
    QSet<QString> m_whitelist;
    std::atomic<int> m_blockedCount{0};
    bool m_enabled = true;

    // 성능: 도메인 기반 빠른 매칭
    QHash<QString, QList<int>> m_domainIndex;  // domain -> rule indices
    QStringList m_adDomains;   // 알려진 광고 도메인 목록
};

// ============================================================
// SecurityInterceptor — 보안 검사 인터셉터
// ============================================================
class SecurityInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit SecurityInterceptor(QObject* parent = nullptr);

    void interceptRequest(QWebEngineUrlRequestInfo& info) override;

    void setEnabled(bool enabled);
    bool isEnabled() const;

signals:
    void threatDetected(const QUrl& url, const QString& threatType);

private:
    bool isKnownMalicious(const QUrl& url) const;
    bool isPhishing(const QUrl& url) const;
    bool isCryptoMiner(const QUrl& url) const;

    bool m_enabled = true;
    QSet<QString> m_maliciousDomains;
    QSet<QString> m_phishingDomains;
    QSet<QString> m_minerDomains;
};

// ============================================================
// OrdinalProfile — 브라우저 프로필 관리
// ============================================================
class OrdinalProfile : public QObject {
    Q_OBJECT
public:
    explicit OrdinalProfile(const QString& storagePath, QObject* parent = nullptr);
    ~OrdinalProfile() override;

    QWebEngineProfile* profile() const;

    // 인터셉터
    AdBlockInterceptor* adBlocker() const;
    SecurityInterceptor* securityInterceptor() const;

    // 설정
    void setDoNotTrack(bool enabled);
    void setJavaScriptEnabled(bool enabled);
    void setWebRtcPolicy(int policy);
    void setUserAgent(const QString& ua);
    void clearBrowsingData();

    // 다운로드 관리
    void setDownloadPath(const QString& path);

    // 쿠키
    void clearCookies();
    void blockThirdPartyCookies(bool block);

signals:
    void downloadRequested(QWebEngineDownloadRequest* download);

private:
    void setupProfile();
    void setupInterceptors();
    void loadDefaultAdBlockRules();

    QWebEngineProfile* m_profile = nullptr;
    AdBlockInterceptor* m_adBlocker = nullptr;
    SecurityInterceptor* m_securityInterceptor = nullptr;
    QString m_storagePath;
};

} // namespace Engine
} // namespace Ordinal
