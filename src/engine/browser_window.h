#pragma once
#include <QMainWindow>
#include <QTabWidget>
#include <QLineEdit>
#include <QToolBar>
#include <QStatusBar>
#include <QMenuBar>
#include <QProgressBar>
#include <QLabel>
#include <QShortcut>
#include <QMap>
#include <QWebEngineDownloadRequest>
#include "web_engine.h"

namespace Ordinal {
namespace Engine {

class BrowserWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BrowserWindow(QWidget* parent = nullptr);
    ~BrowserWindow() override;

    // 탭 관리
    OrdinalWebView* createTab(const QUrl& url = QUrl("ordinal://newtab"));
    void closeTab(int index);
    void closeCurrentTab();
    OrdinalWebView* currentWebView() const;
    int tabCount() const;

    // 네비게이션
    void navigateTo(const QString& urlOrSearch);

private slots:
    void onTabChanged(int index);
    void onTabCloseRequested(int index);

    void onTitleChanged(const QString& title);
    void onUrlChanged(const QUrl& url);
    void onLoadStarted();
    void onLoadProgress(int progress);
    void onLoadFinished(bool ok);
    void onSecurityLevelChanged(OrdinalWebPage::SecurityLevel level);

    void onNewTabRequested(const QUrl& url);
    void onDownloadRequested(QWebEngineDownloadRequest* download);

    // 메뉴 액션
    void onNewTab();
    void onNewWindow();
    void onCloseTab();
    void onReloadPage();
    void onGoBack();
    void onGoForward();
    void onGoHome();
    void onFocusUrlBar();
    void onToggleFullScreen();
    void onFindInPage();
    void onViewSource();
    void onOpenDevTools();
    void onZoomIn();
    void onZoomOut();
    void onZoomReset();
    void onClearData();
    void onAbout();
    void onToggleAdBlock();

private:
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupShortcuts();
    void setupNewTabPage();

    void updateTabInfo(int index);
    void updateSecurityIcon(OrdinalWebPage::SecurityLevel level);
    void updateAddressBar(const QUrl& url);

    QString generateNewTabHtml() const;

    // UI 컴포넌트
    QTabWidget* m_tabWidget = nullptr;
    QLineEdit* m_urlBar = nullptr;
    QToolBar* m_navToolBar = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_securityIcon = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_adBlockLabel = nullptr;

    // 네비게이션 버튼
    QAction* m_backAction = nullptr;
    QAction* m_forwardAction = nullptr;
    QAction* m_reloadAction = nullptr;
    QAction* m_homeAction = nullptr;

    // 프로필
    OrdinalProfile* m_profile = nullptr;

    // Find bar
    QWidget* m_findBar = nullptr;
    QLineEdit* m_findInput = nullptr;
};

} // namespace Engine
} // namespace Ordinal
