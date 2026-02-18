#include "browser_window.h"
#include "settings_page.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QStyle>
#include <QKeySequence>
#include <QShortcut>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenu>
#include <QStandardPaths>
#include <QFileDialog>
#include <QListWidget>
#include <QPushButton>
#include <QDir>
#include <QDesktopServices>
#include <QClipboard>
#include <QTabBar>
#include <iostream>

namespace Ordinal {
namespace Engine {

BrowserWindow::BrowserWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // 프로필 초기화
    QString storagePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + "/OrdinalBrowser";
    QDir().mkpath(storagePath);
    m_profile = new OrdinalProfile(storagePath, this);

    // 데이터 매니저 초기화
    m_bookmarks = new BookmarkManager(storagePath, this);
    m_history = new HistoryManager(storagePath, this);
    m_session = new SessionManager(storagePath, this);

    setupUI();
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupShortcuts();

    // 다운로드 시그널
    connect(m_profile, &OrdinalProfile::downloadRequested,
            this, &BrowserWindow::onDownloadRequested);

    // 세션 복원 또는 새 탭
    if (m_session->hasSession()) {
        onRestoreSession();
    } else {
        createTab(QUrl("https://duckduckgo.com"));
    }

    // 자동 세션 저장 (30초마다)
    m_session->startAutoSave([this]() -> SessionData {
        SessionData data;
        for (int i = 0; i < m_tabWidget->count(); ++i) {
            auto* view = qobject_cast<OrdinalWebView*>(m_tabWidget->widget(i));
            if (!view) continue;
            TabState state;
            state.url = view->currentUrl();
            state.title = view->currentTitle();
            state.lastAccessed = QDateTime::currentDateTime();
            data.tabs.append(state);
        }
        data.activeTabIndex = m_tabWidget->currentIndex();
        return data;
    });
}

BrowserWindow::~BrowserWindow() = default;

// ============================================================
// 탭 관리
// ============================================================

OrdinalWebView* BrowserWindow::createTab(const QUrl& url)
{
    auto* webView = new OrdinalWebView(m_profile->profile(), this);

    int index = m_tabWidget->addTab(webView, "새 탭");
    m_tabWidget->setCurrentIndex(index);

    // 시그널 연결
    connect(webView, &OrdinalWebView::pageTitleChanged, this, [this, webView](const QString& title) {
        int idx = m_tabWidget->indexOf(webView);
        if (idx >= 0) {
            QString shortTitle = title.left(30) + (title.length() > 30 ? "..." : "");
            m_tabWidget->setTabText(idx, shortTitle);
            m_tabWidget->setTabToolTip(idx, title);
        }
        if (webView == currentWebView()) {
            setWindowTitle(title + " — Ordinal Browser");
        }
    });

    connect(webView, &OrdinalWebView::pageUrlChanged, this, [this, webView](const QUrl& url) {
        if (webView == currentWebView()) {
            updateAddressBar(url);
        }
    });

    connect(webView, &OrdinalWebView::pageIconChanged, this, [this, webView](const QIcon& icon) {
        int idx = m_tabWidget->indexOf(webView);
        if (idx >= 0) {
            m_tabWidget->setTabIcon(idx, icon);
        }
    });

    connect(webView, &OrdinalWebView::pageLoadStarted, this, [this, webView]() {
        if (webView == currentWebView()) onLoadStarted();
    });

    connect(webView, &OrdinalWebView::pageLoadProgress, this, [this, webView](int progress) {
        if (webView == currentWebView()) onLoadProgress(progress);
    });

    connect(webView, &OrdinalWebView::pageLoadFinished, this, [this, webView](bool ok) {
        if (webView == currentWebView()) onLoadFinished(ok);
    });

    connect(webView, &OrdinalWebView::securityLevelChanged, this,
            [this, webView](OrdinalWebPage::SecurityLevel level) {
        if (webView == currentWebView()) onSecurityLevelChanged(level);
    });

    connect(webView, &OrdinalWebView::newTabRequested, this, &BrowserWindow::onNewTabRequested);

    // 페이지 로드
    if (!url.isEmpty()) {
        webView->navigate(url);
    }

    return webView;
}

void BrowserWindow::closeTab(int index)
{
    if (m_tabWidget->count() <= 1) {
        // 마지막 탭이면 새 탭 열고 이전 탭 닫기
        createTab(QUrl("https://duckduckgo.com"));
    }

    auto* webView = qobject_cast<OrdinalWebView*>(m_tabWidget->widget(index));
    m_tabWidget->removeTab(index);
    if (webView) {
        webView->deleteLater();
    }
}

void BrowserWindow::closeCurrentTab()
{
    closeTab(m_tabWidget->currentIndex());
}

OrdinalWebView* BrowserWindow::currentWebView() const
{
    return qobject_cast<OrdinalWebView*>(m_tabWidget->currentWidget());
}

int BrowserWindow::tabCount() const
{
    return m_tabWidget->count();
}

void BrowserWindow::navigateTo(const QString& urlOrSearch)
{
    if (auto* view = currentWebView()) {
        view->navigate(urlOrSearch);
    }
}

// ============================================================
// Slots
// ============================================================

void BrowserWindow::onTabChanged(int index)
{
    auto* webView = qobject_cast<OrdinalWebView*>(m_tabWidget->widget(index));
    if (!webView) return;

    updateAddressBar(webView->currentUrl());
    setWindowTitle(webView->currentTitle() + " — Ordinal Browser");

    m_backAction->setEnabled(webView->history()->canGoBack());
    m_forwardAction->setEnabled(webView->history()->canGoForward());
    onSecurityLevelChanged(webView->securityLevel());
}

void BrowserWindow::onTabCloseRequested(int index)
{
    closeTab(index);
}

void BrowserWindow::onTitleChanged(const QString& title)
{
    setWindowTitle(title + " — Ordinal Browser");
}

void BrowserWindow::onUrlChanged(const QUrl& url)
{
    updateAddressBar(url);
}

void BrowserWindow::onLoadStarted()
{
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_reloadAction->setIcon(style()->standardIcon(QStyle::SP_BrowserStop));
    m_reloadAction->setToolTip("중지");
    m_statusLabel->setText("로딩 중...");
}

void BrowserWindow::onLoadProgress(int progress)
{
    m_progressBar->setValue(progress);
}

void BrowserWindow::onLoadFinished(bool ok)
{
    m_progressBar->setVisible(false);
    m_reloadAction->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_reloadAction->setToolTip("새로고침 (F5)");

    if (ok) {
        int blocked = m_profile->adBlocker()->blockedCount();
        m_statusLabel->setText("완료");
        m_adBlockLabel->setText(QString("🛡 %1 차단").arg(blocked));

        // 방문 기록 저장
        auto* view = currentWebView();
        if (view && m_history) {
            m_history->addVisit(view->currentUrl(), view->currentTitle());
        }

        // 북마크 버튼 상태 업데이트
        if (view && m_bookmarkAction && m_bookmarks) {
            bool bookmarked = m_bookmarks->isBookmarked(view->currentUrl());
            m_bookmarkAction->setText(bookmarked ? "★" : "☆");
            m_bookmarkAction->setToolTip(bookmarked ? "북마크 제거" : "북마크 추가");
        }
    } else {
        m_statusLabel->setText("로딩 실패");
    }

    auto* view = currentWebView();
    if (view) {
        m_backAction->setEnabled(view->history()->canGoBack());
        m_forwardAction->setEnabled(view->history()->canGoForward());
    }
}

void BrowserWindow::onSecurityLevelChanged(OrdinalWebPage::SecurityLevel level)
{
    updateSecurityIcon(level);
}

void BrowserWindow::onNewTabRequested(const QUrl& url)
{
    createTab(url);
}

void BrowserWindow::onDownloadRequested(QWebEngineDownloadRequest* download)
{
    QString defaultPath = download->downloadDirectory() + "/" + download->downloadFileName();
    QString path = QFileDialog::getSaveFileName(this, "다운로드 저장",
                                                 defaultPath);
    if (path.isEmpty()) {
        download->cancel();
        return;
    }

    QFileInfo fi(path);
    download->setDownloadDirectory(fi.absolutePath());
    download->setDownloadFileName(fi.fileName());
    download->accept();

    m_statusLabel->setText("다운로드 중: " + fi.fileName());

    connect(download, &QWebEngineDownloadRequest::isFinishedChanged, this, [this, download]() {
        if (download->isFinished()) {
            m_statusLabel->setText("다운로드 완료: " + download->downloadFileName());
        }
    });
}

void BrowserWindow::onNewTab() { createTab(QUrl("https://duckduckgo.com")); }
void BrowserWindow::onNewWindow()
{
    auto* w = new BrowserWindow();
    w->resize(1280, 800);
    w->show();
}
void BrowserWindow::onCloseTab() { closeCurrentTab(); }

void BrowserWindow::onReloadPage()
{
    if (auto* view = currentWebView()) {
        if (view->isLoading()) view->stopLoading();
        else view->reload();
    }
}

void BrowserWindow::onGoBack() { if (auto* v = currentWebView()) v->goBack(); }
void BrowserWindow::onGoForward() { if (auto* v = currentWebView()) v->goForward(); }
void BrowserWindow::onGoHome() { if (auto* v = currentWebView()) v->navigate(QUrl("https://duckduckgo.com")); }

void BrowserWindow::onFocusUrlBar()
{
    m_urlBar->setFocus();
    m_urlBar->selectAll();
}

void BrowserWindow::onToggleFullScreen()
{
    if (isFullScreen()) showNormal();
    else showFullScreen();
}

void BrowserWindow::onFindInPage()
{
    if (!m_findBar) {
        m_findBar = new QWidget(this);
        auto* layout = new QHBoxLayout(m_findBar);
        layout->setContentsMargins(4, 2, 4, 2);

        m_findInput = new QLineEdit(m_findBar);
        m_findInput->setPlaceholderText("페이지에서 찾기...");
        m_findInput->setMaximumWidth(300);

        auto* findNext = new QToolButton(m_findBar);
        findNext->setText("▼");
        findNext->setToolTip("다음");

        auto* findPrev = new QToolButton(m_findBar);
        findPrev->setText("▲");
        findPrev->setToolTip("이전");

        auto* closeBtn = new QToolButton(m_findBar);
        closeBtn->setText("✕");

        layout->addWidget(m_findInput);
        layout->addWidget(findPrev);
        layout->addWidget(findNext);
        layout->addStretch();
        layout->addWidget(closeBtn);

        connect(m_findInput, &QLineEdit::textChanged, this, [this](const QString& text) {
            if (auto* v = currentWebView()) v->findText(text);
        });
        connect(m_findInput, &QLineEdit::returnPressed, this, [this]() {
            if (auto* v = currentWebView()) v->findText(m_findInput->text());
        });
        connect(findNext, &QToolButton::clicked, this, [this]() {
            if (auto* v = currentWebView()) v->findText(m_findInput->text(), true);
        });
        connect(findPrev, &QToolButton::clicked, this, [this]() {
            if (auto* v = currentWebView()) v->findText(m_findInput->text(), false);
        });
        connect(closeBtn, &QToolButton::clicked, this, [this]() {
            m_findBar->hide();
            if (auto* v = currentWebView()) v->clearFind();
        });

        statusBar()->addPermanentWidget(m_findBar);
    }

    m_findBar->show();
    m_findInput->setFocus();
    m_findInput->selectAll();
}

void BrowserWindow::onViewSource()
{
    if (auto* v = currentWebView()) v->viewSource();
}

void BrowserWindow::onOpenDevTools()
{
    if (auto* v = currentWebView()) v->openDevTools();
}

void BrowserWindow::onZoomIn() { if (auto* v = currentWebView()) v->zoomIn(); }
void BrowserWindow::onZoomOut() { if (auto* v = currentWebView()) v->zoomOut(); }
void BrowserWindow::onZoomReset() { if (auto* v = currentWebView()) v->resetZoom(); }

void BrowserWindow::onClearData()
{
    auto reply = QMessageBox::question(this, "브라우징 데이터 삭제",
        "캐시, 쿠키, 히스토리를 모두 삭제하시겠습니까?");
    if (reply == QMessageBox::Yes) {
        m_profile->clearBrowsingData();
        m_statusLabel->setText("브라우징 데이터 삭제 완료");
    }
}

void BrowserWindow::onAbout()
{
    QMessageBox::about(this, "Ordinal Browser",
        "<h2>Ordinal Browser v1.2.0</h2>"
        "<p>AI 기반 보안 웹 브라우저</p>"
        "<p>Chromium 엔진 (Qt WebEngine) 기반<br>"
        "LLM Security Agent 내장<br>"
        "광고 차단 / 추적 방지 / WebRTC 보호</p>"
        "<p>© 2026 KaztoRay / Ordinal Project</p>"
        "<p><a href='https://github.com/KaztoRay/Ordinal-Browser'>GitHub</a></p>");
}

void BrowserWindow::onToggleAdBlock()
{
    bool enabled = m_profile->adBlocker()->isEnabled();
    m_profile->adBlocker()->setEnabled(!enabled);
    m_statusLabel->setText(enabled ? "광고 차단 비활성화" : "광고 차단 활성화");
}

void BrowserWindow::onOpenSettings()
{
    auto* settings = new SettingsPage(m_profile, this);
    settings->exec();
    settings->deleteLater();
}

void BrowserWindow::onToggleBookmark()
{
    auto* view = currentWebView();
    if (!view || !m_bookmarks) return;

    QUrl url = view->currentUrl();
    auto existing = m_bookmarks->findByUrl(url);
    if (existing) {
        m_bookmarks->removeBookmark(existing->id);
        m_bookmarkAction->setText("☆");
        m_bookmarkAction->setToolTip("북마크 추가 (Ctrl+D)");
        m_statusLabel->setText("북마크 제거됨");
    } else {
        m_bookmarks->addBookmark(view->currentTitle(), url);
        m_bookmarkAction->setText("★");
        m_bookmarkAction->setToolTip("북마크 제거 (Ctrl+D)");
        m_statusLabel->setText("북마크 추가됨");
    }
}

void BrowserWindow::onShowBookmarks()
{
    // 간단한 북마크 목록 다이얼로그
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("북마크");
    dialog->resize(400, 500);
    auto* layout = new QVBoxLayout(dialog);

    auto* list = new QListWidget(dialog);
    auto bookmarks = m_bookmarks->getRecent(100);
    for (const auto& bm : bookmarks) {
        auto* item = new QListWidgetItem(bm.title + "\n" + bm.url.toString(), list);
        item->setData(Qt::UserRole, bm.url);
    }
    layout->addWidget(list);

    connect(list, &QListWidget::itemDoubleClicked, this, [this, dialog](QListWidgetItem* item) {
        QUrl url = item->data(Qt::UserRole).toUrl();
        if (auto* v = currentWebView()) v->navigate(url);
        dialog->accept();
    });

    dialog->exec();
    dialog->deleteLater();
}

void BrowserWindow::onShowHistory()
{
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("방문 기록");
    dialog->resize(500, 600);
    auto* layout = new QVBoxLayout(dialog);

    // 검색
    auto* searchBar = new QLineEdit(dialog);
    searchBar->setPlaceholderText("기록 검색...");
    layout->addWidget(searchBar);

    auto* list = new QListWidget(dialog);
    auto recent = m_history->getRecent(200);
    for (const auto& entry : recent) {
        auto* item = new QListWidgetItem(
            entry.title + "\n" + entry.url.toString() +
            "\n" + entry.visitTime.toString("yyyy-MM-dd hh:mm"), list);
        item->setData(Qt::UserRole, entry.url);
    }
    layout->addWidget(list);

    connect(searchBar, &QLineEdit::textChanged, this, [this, list](const QString& text) {
        list->clear();
        auto results = text.isEmpty() ? m_history->getRecent(200) : m_history->search(text);
        for (const auto& entry : results) {
            auto* item = new QListWidgetItem(
                entry.title + "\n" + entry.url.toString(), list);
            item->setData(Qt::UserRole, entry.url);
        }
    });

    connect(list, &QListWidget::itemDoubleClicked, this, [this, dialog](QListWidgetItem* item) {
        QUrl url = item->data(Qt::UserRole).toUrl();
        if (auto* v = currentWebView()) v->navigate(url);
        dialog->accept();
    });

    // 삭제 버튼
    auto* clearBtn = new QPushButton("전체 기록 삭제", dialog);
    connect(clearBtn, &QPushButton::clicked, this, [this, list, dialog]() {
        auto reply = QMessageBox::question(dialog, "기록 삭제", "모든 방문 기록을 삭제하시겠습니까?");
        if (reply == QMessageBox::Yes) {
            m_history->clearAll();
            list->clear();
        }
    });
    layout->addWidget(clearBtn);

    dialog->exec();
    dialog->deleteLater();
}

void BrowserWindow::onRestoreSession()
{
    auto session = m_session->loadSession();
    if (session.tabs.isEmpty()) {
        createTab(QUrl("https://duckduckgo.com"));
        return;
    }

    for (const auto& tab : session.tabs) {
        createTab(tab.url);
    }

    if (session.activeTabIndex >= 0 && session.activeTabIndex < m_tabWidget->count()) {
        m_tabWidget->setCurrentIndex(session.activeTabIndex);
    }

    m_statusLabel->setText(QString("세션 복원: %1개 탭").arg(session.tabs.size()));
}

// ============================================================
// UI Setup
// ============================================================

void BrowserWindow::setupUI()
{
    setWindowTitle("Ordinal Browser");
    resize(1280, 800);

    // 탭 위젯
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setElideMode(Qt::ElideRight);

    // 새 탭 버튼
    auto* newTabBtn = new QToolButton(m_tabWidget);
    newTabBtn->setText("+");
    newTabBtn->setToolTip("새 탭 (Ctrl+T)");
    newTabBtn->setAutoRaise(true);
    m_tabWidget->setCornerWidget(newTabBtn, Qt::TopRightCorner);
    connect(newTabBtn, &QToolButton::clicked, this, &BrowserWindow::onNewTab);

    connect(m_tabWidget, &QTabWidget::currentChanged, this, &BrowserWindow::onTabChanged);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &BrowserWindow::onTabCloseRequested);

    // 탭 우클릭 컨텍스트 메뉴
    m_tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabWidget->tabBar(), &QWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        int tabIndex = m_tabWidget->tabBar()->tabAt(pos);
        if (tabIndex < 0) return;

        QMenu menu;
        menu.addAction("새 탭", this, &BrowserWindow::onNewTab);
        menu.addSeparator();
        menu.addAction("새로고침", this, [this, tabIndex]() {
            auto* view = qobject_cast<OrdinalWebView*>(m_tabWidget->widget(tabIndex));
            if (view) view->reload();
        });
        menu.addAction("복제", this, [this, tabIndex]() {
            auto* view = qobject_cast<OrdinalWebView*>(m_tabWidget->widget(tabIndex));
            if (view) createTab(view->currentUrl());
        });
        menu.addSeparator();
        menu.addAction("이 탭 닫기", this, [this, tabIndex]() { closeTab(tabIndex); });
        menu.addAction("다른 탭 모두 닫기", this, [this, tabIndex]() {
            for (int i = m_tabWidget->count() - 1; i >= 0; --i) {
                if (i != tabIndex) closeTab(i);
            }
        });
        menu.addAction("오른쪽 탭 모두 닫기", this, [this, tabIndex]() {
            for (int i = m_tabWidget->count() - 1; i > tabIndex; --i) {
                closeTab(i);
            }
        });
        menu.addSeparator();
        menu.addAction("URL 복사", this, [this, tabIndex]() {
            auto* view = qobject_cast<OrdinalWebView*>(m_tabWidget->widget(tabIndex));
            if (view) QApplication::clipboard()->setText(view->currentUrl().toString());
        });

        menu.exec(m_tabWidget->tabBar()->mapToGlobal(pos));
    });

    setCentralWidget(m_tabWidget);
}

void BrowserWindow::setupMenuBar()
{
    // 파일 메뉴
    auto* fileMenu = menuBar()->addMenu("파일(&F)");
    fileMenu->addAction("새 탭", this, &BrowserWindow::onNewTab, QKeySequence::AddTab);
    fileMenu->addAction("새 창", this, &BrowserWindow::onNewWindow, QKeySequence("Ctrl+Shift+N"));
    fileMenu->addSeparator();
    fileMenu->addAction("탭 닫기", this, &BrowserWindow::onCloseTab, QKeySequence::Close);
    fileMenu->addSeparator();
    fileMenu->addAction("종료", this, &QWidget::close, QKeySequence::Quit);

    // 편집 메뉴
    auto* editMenu = menuBar()->addMenu("편집(&E)");
    editMenu->addAction("페이지에서 찾기", this, &BrowserWindow::onFindInPage, QKeySequence::Find);
    editMenu->addSeparator();
    editMenu->addAction("소스 보기", this, &BrowserWindow::onViewSource, QKeySequence("Ctrl+U"));

    // 보기 메뉴
    auto* viewMenu = menuBar()->addMenu("보기(&V)");
    viewMenu->addAction("확대", this, &BrowserWindow::onZoomIn, QKeySequence::ZoomIn);
    viewMenu->addAction("축소", this, &BrowserWindow::onZoomOut, QKeySequence::ZoomOut);
    viewMenu->addAction("원래 크기", this, &BrowserWindow::onZoomReset, QKeySequence("Ctrl+0"));
    viewMenu->addSeparator();
    viewMenu->addAction("전체 화면", this, &BrowserWindow::onToggleFullScreen, QKeySequence("F11"));
    viewMenu->addSeparator();
    viewMenu->addAction("개발자 도구", this, &BrowserWindow::onOpenDevTools, QKeySequence("F12"));

    // 북마크 메뉴
    auto* bookmarkMenu = menuBar()->addMenu("북마크(&B)");
    bookmarkMenu->addAction("북마크 추가/제거", this, &BrowserWindow::onToggleBookmark, QKeySequence("Ctrl+D"));
    bookmarkMenu->addAction("북마크 관리", this, &BrowserWindow::onShowBookmarks, QKeySequence("Ctrl+Shift+B"));

    // 히스토리 메뉴
    auto* historyMenu = menuBar()->addMenu("기록(&I)");
    historyMenu->addAction("방문 기록", this, &BrowserWindow::onShowHistory, QKeySequence("Ctrl+H"));

    // 보안 메뉴
    auto* securityMenu = menuBar()->addMenu("보안(&S)");
    securityMenu->addAction("광고 차단 토글", this, &BrowserWindow::onToggleAdBlock);
    securityMenu->addSeparator();
    securityMenu->addAction("브라우징 데이터 삭제", this, &BrowserWindow::onClearData,
                            QKeySequence("Ctrl+Shift+Delete"));

    // 도구 메뉴
    auto* toolsMenu = menuBar()->addMenu("도구(&T)");
    toolsMenu->addAction("설정", this, &BrowserWindow::onOpenSettings, QKeySequence("Ctrl+,"));

    // 도움말 메뉴
    auto* helpMenu = menuBar()->addMenu("도움말(&H)");
    helpMenu->addAction("정보", this, &BrowserWindow::onAbout);
}

void BrowserWindow::setupToolBar()
{
    m_navToolBar = addToolBar("네비게이션");
    m_navToolBar->setMovable(false);
    m_navToolBar->setIconSize(QSize(18, 18));

    // 뒤로
    m_backAction = m_navToolBar->addAction(
        style()->standardIcon(QStyle::SP_ArrowBack), "뒤로");
    m_backAction->setShortcut(QKeySequence::Back);
    m_backAction->setEnabled(false);
    connect(m_backAction, &QAction::triggered, this, &BrowserWindow::onGoBack);

    // 앞으로
    m_forwardAction = m_navToolBar->addAction(
        style()->standardIcon(QStyle::SP_ArrowForward), "앞으로");
    m_forwardAction->setShortcut(QKeySequence::Forward);
    m_forwardAction->setEnabled(false);
    connect(m_forwardAction, &QAction::triggered, this, &BrowserWindow::onGoForward);

    // 새로고침
    m_reloadAction = m_navToolBar->addAction(
        style()->standardIcon(QStyle::SP_BrowserReload), "새로고침");
    m_reloadAction->setShortcut(QKeySequence::Refresh);
    connect(m_reloadAction, &QAction::triggered, this, &BrowserWindow::onReloadPage);

    // 홈
    m_homeAction = m_navToolBar->addAction(
        style()->standardIcon(QStyle::SP_DirHomeIcon), "홈");
    connect(m_homeAction, &QAction::triggered, this, &BrowserWindow::onGoHome);

    // 보안 아이콘
    m_securityIcon = new QLabel("🔒", this);
    m_securityIcon->setMinimumWidth(24);
    m_securityIcon->setAlignment(Qt::AlignCenter);
    m_navToolBar->addWidget(m_securityIcon);

    // URL 바
    m_urlBar = new QLineEdit(this);
    m_urlBar->setPlaceholderText("URL 또는 검색어 입력...");
    m_urlBar->setClearButtonEnabled(true);
    m_urlBar->setMinimumHeight(28);
    m_urlBar->setStyleSheet(
        "QLineEdit {"
        "  border: 1px solid #ccc; border-radius: 14px;"
        "  padding: 4px 12px; font-size: 13px;"
        "  background: #f5f5f5;"
        "}"
        "QLineEdit:focus {"
        "  border-color: #4285f4; background: white;"
        "}");

    connect(m_urlBar, &QLineEdit::returnPressed, this, [this]() {
        navigateTo(m_urlBar->text());
        if (auto* v = currentWebView()) v->setFocus();
    });

    m_navToolBar->addWidget(m_urlBar);

    // 북마크 버튼
    m_bookmarkAction = m_navToolBar->addAction("☆");
    m_bookmarkAction->setToolTip("북마크 추가 (Ctrl+D)");
    connect(m_bookmarkAction, &QAction::triggered, this, &BrowserWindow::onToggleBookmark);
}

void BrowserWindow::setupStatusBar()
{
    m_progressBar = new QProgressBar(this);
    m_progressBar->setMaximumHeight(3);
    m_progressBar->setTextVisible(false);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { border: none; background: transparent; }"
        "QProgressBar::chunk { background: #4285f4; }");

    m_statusLabel = new QLabel("준비", this);
    m_adBlockLabel = new QLabel("🛡 0 차단", this);

    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_adBlockLabel);

    // 프로그레스바를 툴바 아래에 배치
    auto* centralLayout = qobject_cast<QVBoxLayout*>(centralWidget()->layout());
    Q_UNUSED(centralLayout)
}

void BrowserWindow::setupShortcuts()
{
    new QShortcut(QKeySequence("Ctrl+L"), this, [this]() { onFocusUrlBar(); });
    new QShortcut(QKeySequence("Alt+D"), this, [this]() { onFocusUrlBar(); });
    new QShortcut(QKeySequence("Ctrl+Tab"), this, [this]() {
        int next = (m_tabWidget->currentIndex() + 1) % m_tabWidget->count();
        m_tabWidget->setCurrentIndex(next);
    });
    new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this, [this]() {
        int prev = (m_tabWidget->currentIndex() - 1 + m_tabWidget->count()) % m_tabWidget->count();
        m_tabWidget->setCurrentIndex(prev);
    });
    // Ctrl+1~9로 탭 전환
    for (int i = 1; i <= 9; ++i) {
        new QShortcut(QKeySequence(QString("Ctrl+%1").arg(i)), this, [this, i]() {
            int idx = (i == 9) ? m_tabWidget->count() - 1 : i - 1;
            if (idx < m_tabWidget->count()) m_tabWidget->setCurrentIndex(idx);
        });
    }
}

// ============================================================
// Private helpers
// ============================================================

void BrowserWindow::updateSecurityIcon(OrdinalWebPage::SecurityLevel level)
{
    switch (level) {
    case OrdinalWebPage::SecurityLevel::Safe:
        m_securityIcon->setText("🔒");
        m_securityIcon->setToolTip("보안 연결 (HTTPS)");
        break;
    case OrdinalWebPage::SecurityLevel::Warning:
        m_securityIcon->setText("⚠️");
        m_securityIcon->setToolTip("비보안 연결 (HTTP)");
        break;
    case OrdinalWebPage::SecurityLevel::Dangerous:
        m_securityIcon->setText("🔓");
        m_securityIcon->setToolTip("위험한 연결");
        break;
    case OrdinalWebPage::SecurityLevel::Unknown:
    default:
        m_securityIcon->setText("ℹ️");
        m_securityIcon->setToolTip("보안 상태 확인 중");
        break;
    }
}

void BrowserWindow::updateAddressBar(const QUrl& url)
{
    QString display = url.toString();
    if (display.startsWith("ordinal://")) {
        m_urlBar->setText("");
    } else {
        m_urlBar->setText(display);
    }
}

} // namespace Engine
} // namespace Ordinal
