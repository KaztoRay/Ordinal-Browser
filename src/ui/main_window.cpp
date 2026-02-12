/**
 * @file main_window.cpp
 * @brief 메인 윈도우 구현
 * 
 * Qt6 기반 브라우저 메인 윈도우의 UI 구성, 메뉴, 툴바,
 * 탭 관리, 보안 표시기를 구현합니다.
 */

#include "main_window.h"
#include "tab_bar.h"
#include "address_bar.h"
#include "security_panel.h"
#include "dev_tools_panel.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QShortcut>
#include <QMessageBox>
#include <QStyle>
#include <QIcon>
#include <QFont>
#include <QPalette>

namespace ordinal::ui {

// ============================================================
// 생성자 / 소멸자
// ============================================================

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // 윈도우 기본 설정
    setWindowTitle("Ordinal Browser");
    setMinimumSize(1024, 768);
    resize(1400, 900);

    // 다크 테마 팔레트 적용
    QPalette dark_palette;
    dark_palette.setColor(QPalette::Window, QColor(30, 30, 35));
    dark_palette.setColor(QPalette::WindowText, QColor(220, 220, 225));
    dark_palette.setColor(QPalette::Base, QColor(25, 25, 30));
    dark_palette.setColor(QPalette::AlternateBase, QColor(40, 40, 45));
    dark_palette.setColor(QPalette::ToolTipBase, QColor(50, 50, 55));
    dark_palette.setColor(QPalette::ToolTipText, QColor(220, 220, 225));
    dark_palette.setColor(QPalette::Text, QColor(220, 220, 225));
    dark_palette.setColor(QPalette::Button, QColor(45, 45, 50));
    dark_palette.setColor(QPalette::ButtonText, QColor(220, 220, 225));
    dark_palette.setColor(QPalette::Highlight, QColor(70, 130, 220));
    dark_palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    setPalette(dark_palette);

    // UI 구성
    setupMenuBar();
    setupToolBar();
    setupCentralWidget();
    setupStatusBar();
    setupShortcuts();

    // 설정 로드
    loadSettings();
}

MainWindow::~MainWindow() {
    saveSettings();
}

// ============================================================
// 탭 관리
// ============================================================

int MainWindow::openNewTab(const QString& url) {
    // 새 페이지 위젯 생성
    auto* page_widget = new QWidget();
    auto* layout = new QVBoxLayout(page_widget);
    layout->setContentsMargins(0, 0, 0, 0);

    // 빈 콘텐츠 라벨 (실제로는 렌더러 뷰가 들어갈 자리)
    auto* content_label = new QLabel(url.isEmpty() ? "새 탭" : url);
    content_label->setAlignment(Qt::AlignCenter);
    content_label->setStyleSheet("QLabel { color: #888; font-size: 18px; }");
    layout->addWidget(content_label);

    int index = page_stack_->addWidget(page_widget);

    // 탭 바에 탭 추가
    if (tab_bar_) {
        QString tab_title = url.isEmpty() ? "새 탭" : url;
        tab_bar_->addNewTab(tab_title);
    }

    // 새 탭으로 전환
    page_stack_->setCurrentIndex(index);

    if (!url.isEmpty()) {
        navigateTo(url);
    }

    return index;
}

void MainWindow::closeCurrentTab() {
    closeTab(currentTabIndex());
}

void MainWindow::closeTab(int index) {
    if (tabCount() <= 1) {
        // 마지막 탭 닫기 시 새 빈 탭 생성
        openNewTab();
    }

    if (index >= 0 && index < page_stack_->count()) {
        auto* widget = page_stack_->widget(index);
        page_stack_->removeWidget(widget);
        delete widget;

        if (tab_bar_) {
            tab_bar_->removeTab(index);
        }
    }
}

int MainWindow::currentTabIndex() const {
    return page_stack_ ? page_stack_->currentIndex() : -1;
}

int MainWindow::tabCount() const {
    return page_stack_ ? page_stack_->count() : 0;
}

// ============================================================
// 네비게이션
// ============================================================

void MainWindow::navigateTo(const QString& url) {
    QString processed_url = url;

    // URL 스킴이 없으면 추가
    if (!processed_url.startsWith("http://") && !processed_url.startsWith("https://") &&
        !processed_url.startsWith("ordinal://")) {
        // 도메인 패턴 확인
        if (processed_url.contains('.') && !processed_url.contains(' ')) {
            processed_url = "https://" + processed_url;
        } else {
            // 검색 엔진으로 전달
            processed_url = "https://www.google.com/search?q=" + processed_url;
        }
    }

    // 주소 바 업데이트
    if (address_bar_) {
        address_bar_->setUrl(processed_url);
    }

    // 상태 바 업데이트
    if (status_label_) {
        status_label_->setText("로딩 중: " + processed_url);
    }

    // URL 변경 시그널 발생
    emit urlChanged(processed_url);

    // 로딩 시작 시뮬레이션
    onLoadStarted();

    // 탭 타이틀 업데이트
    if (tab_bar_ && currentTabIndex() >= 0) {
        tab_bar_->setTabTitle(currentTabIndex(), processed_url);
    }
}

void MainWindow::goBack() {
    if (status_label_) {
        status_label_->setText("뒤로 가기");
    }
}

void MainWindow::goForward() {
    if (status_label_) {
        status_label_->setText("앞으로 가기");
    }
}

void MainWindow::reload() {
    if (status_label_) {
        status_label_->setText("새로고침 중...");
    }
    onLoadStarted();
}

void MainWindow::stopLoading() {
    if (status_label_) {
        status_label_->setText("로딩 중지됨");
    }
    if (load_progress_label_) {
        load_progress_label_->clear();
    }
}

// ============================================================
// 보안 상태
// ============================================================

void MainWindow::updateSecurityStatus(SecurityStatus status, const QString& message) {
    current_security_status_ = status;

    if (security_indicator_) {
        switch (status) {
            case SecurityStatus::Secure:
                security_indicator_->setText("🟢 보안");
                security_indicator_->setStyleSheet(
                    "QLabel { color: #4CAF50; font-weight: bold; padding: 2px 8px; }");
                break;
            case SecurityStatus::Warning:
                security_indicator_->setText("🟡 주의");
                security_indicator_->setStyleSheet(
                    "QLabel { color: #FFC107; font-weight: bold; padding: 2px 8px; }");
                break;
            case SecurityStatus::Danger:
                security_indicator_->setText("🔴 위험");
                security_indicator_->setStyleSheet(
                    "QLabel { color: #F44336; font-weight: bold; padding: 2px 8px; }");
                break;
        }
    }

    if (status_label_ && !message.isEmpty()) {
        status_label_->setText(message);
    }

    // 주소 바 보안 아이콘 업데이트
    if (address_bar_) {
        address_bar_->setSecurityStatus(static_cast<int>(status));
    }

    // 보안 패널 업데이트
    if (security_panel_) {
        security_panel_->updateStatus(status, message);
    }

    emit securityStatusChanged(status);
}

void MainWindow::toggleSecurityPanel() {
    security_panel_visible_ = !security_panel_visible_;
    if (security_panel_) {
        security_panel_->setVisible(security_panel_visible_);
    }
}

void MainWindow::toggleDevTools() {
    dev_tools_visible_ = !dev_tools_visible_;
    if (dev_tools_panel_) {
        dev_tools_panel_->setVisible(dev_tools_visible_);
    }
}

// ============================================================
// 이벤트 핸들러
// ============================================================

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    event->accept();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
}

// ============================================================
// 슬롯
// ============================================================

void MainWindow::onTabChanged(int index) {
    if (page_stack_ && index >= 0 && index < page_stack_->count()) {
        page_stack_->setCurrentIndex(index);
    }
    emit tabChanged(index);
}

void MainWindow::onAddressEntered(const QString& url) {
    navigateTo(url);
}

void MainWindow::onLoadStarted() {
    if (load_progress_label_) {
        load_progress_label_->setText("로딩 중...");
    }

    // 진행률 시뮬레이션 (실제로는 네트워크 모듈에서 제공)
    QTimer::singleShot(300, this, [this]() { onLoadProgressUpdate(30); });
    QTimer::singleShot(700, this, [this]() { onLoadProgressUpdate(60); });
    QTimer::singleShot(1000, this, [this]() { onLoadProgressUpdate(90); });
    QTimer::singleShot(1200, this, [this]() { onLoadFinished(true); });
}

void MainWindow::onLoadFinished(bool success) {
    if (success) {
        if (status_label_) status_label_->setText("완료");
        if (load_progress_label_) load_progress_label_->clear();
    } else {
        if (status_label_) status_label_->setText("로딩 실패");
    }
}

void MainWindow::onLoadProgressUpdate(int percent) {
    if (load_progress_label_) {
        load_progress_label_->setText(QString::number(percent) + "%");
    }
    emit loadProgress(percent);
}

void MainWindow::onTitleChanged(const QString& title) {
    if (tab_bar_ && currentTabIndex() >= 0) {
        tab_bar_->setTabTitle(currentTabIndex(), title);
    }
    setWindowTitle(title + " — Ordinal Browser");
}

// ============================================================
// UI 초기화
// ============================================================

void MainWindow::setupMenuBar() {
    createFileMenu();
    createEditMenu();
    createViewMenu();
    createSecurityMenu();
    createHelpMenu();
}

void MainWindow::createFileMenu() {
    auto* menu = menuBar()->addMenu("파일(&F)");

    action_new_tab_ = menu->addAction("새 탭(&T)", this, [this]() {
        openNewTab();
    });
    action_new_tab_->setShortcut(QKeySequence("Ctrl+T"));

    action_close_tab_ = menu->addAction("탭 닫기(&W)", this, [this]() {
        closeCurrentTab();
    });
    action_close_tab_->setShortcut(QKeySequence("Ctrl+W"));

    menu->addSeparator();

    menu->addAction("새 창(&N)", this, []() {
        // 새 윈도우 생성 (간략화)
    })->setShortcut(QKeySequence("Ctrl+N"));

    menu->addSeparator();

    menu->addAction("종료(&Q)", this, [this]() {
        close();
    })->setShortcut(QKeySequence("Ctrl+Q"));
}

void MainWindow::createEditMenu() {
    auto* menu = menuBar()->addMenu("편집(&E)");

    menu->addAction("잘라내기", this, []() {})->setShortcut(QKeySequence("Ctrl+X"));
    menu->addAction("복사", this, []() {})->setShortcut(QKeySequence("Ctrl+C"));
    menu->addAction("붙여넣기", this, []() {})->setShortcut(QKeySequence("Ctrl+V"));
    menu->addSeparator();
    menu->addAction("찾기", this, []() {})->setShortcut(QKeySequence("Ctrl+F"));
    menu->addAction("모두 선택", this, []() {})->setShortcut(QKeySequence("Ctrl+A"));
}

void MainWindow::createViewMenu() {
    auto* menu = menuBar()->addMenu("보기(&V)");

    action_reload_ = menu->addAction("새로고침(&R)", this, [this]() {
        reload();
    });
    action_reload_->setShortcut(QKeySequence("Ctrl+R"));

    action_stop_ = menu->addAction("중지", this, [this]() {
        stopLoading();
    });
    action_stop_->setShortcut(QKeySequence("Escape"));

    menu->addSeparator();

    menu->addAction("확대", this, []() {})->setShortcut(QKeySequence("Ctrl++"));
    menu->addAction("축소", this, []() {})->setShortcut(QKeySequence("Ctrl+-"));
    menu->addAction("원래 크기", this, []() {})->setShortcut(QKeySequence("Ctrl+0"));

    menu->addSeparator();

    action_dev_tools_ = menu->addAction("개발자 도구(&D)", this, [this]() {
        toggleDevTools();
    });
    action_dev_tools_->setShortcut(QKeySequence("F12"));
    action_dev_tools_->setCheckable(true);

    menu->addAction("페이지 소스 보기", this, []() {})->setShortcut(QKeySequence("Ctrl+U"));
}

void MainWindow::createSecurityMenu() {
    auto* menu = menuBar()->addMenu("보안(&S)");

    action_security_panel_ = menu->addAction("보안 패널(&P)", this, [this]() {
        toggleSecurityPanel();
    });
    action_security_panel_->setShortcut(QKeySequence("Ctrl+Shift+S"));
    action_security_panel_->setCheckable(true);

    menu->addSeparator();

    menu->addAction("인증서 정보", this, []() {});
    menu->addAction("트래커 차단 목록", this, []() {});
    menu->addAction("보안 감사 실행", this, []() {});
}

void MainWindow::createHelpMenu() {
    auto* menu = menuBar()->addMenu("도움말(&H)");

    menu->addAction("Ordinal Browser 정보", this, [this]() {
        QMessageBox::about(this, "Ordinal Browser",
            "Ordinal Browser v0.1.0\n\n"
            "AI 기반 보안 웹 브라우저\n"
            "V8 JavaScript 엔진 + LLM 보안 에이전트\n\n"
            "© 2026 Ordinal Project");
    });

    menu->addAction("보안 에이전트 상태", this, []() {});
}

void MainWindow::setupToolBar() {
    // 네비게이션 툴바
    nav_toolbar_ = addToolBar("네비게이션");
    nav_toolbar_->setMovable(false);
    nav_toolbar_->setStyleSheet(
        "QToolBar { background: #1e1e23; border-bottom: 1px solid #333; spacing: 4px; padding: 2px; }"
        "QToolButton { color: #ddd; padding: 4px 8px; border-radius: 4px; }"
        "QToolButton:hover { background: #3a3a45; }"
    );

    action_back_ = nav_toolbar_->addAction("←", this, [this]() { goBack(); });
    action_back_->setToolTip("뒤로 (Alt+←)");

    action_forward_ = nav_toolbar_->addAction("→", this, [this]() { goForward(); });
    action_forward_->setToolTip("앞으로 (Alt+→)");

    action_reload_ = nav_toolbar_->addAction("⟳", this, [this]() { reload(); });
    action_reload_->setToolTip("새로고침 (Ctrl+R)");

    nav_toolbar_->addSeparator();

    // 주소 바 추가
    address_bar_ = new AddressBar(this);
    nav_toolbar_->addWidget(address_bar_);

    // 주소 바 시그널 연결
    connect(address_bar_, &AddressBar::urlEntered, this, &MainWindow::onAddressEntered);
}

void MainWindow::setupStatusBar() {
    auto* status = statusBar();
    status->setStyleSheet(
        "QStatusBar { background: #1a1a1f; color: #888; border-top: 1px solid #333; }"
        "QStatusBar::item { border: none; }"
    );

    // 상태 메시지 라벨
    status_label_ = new QLabel("준비");
    status_label_->setStyleSheet("QLabel { padding: 0 8px; }");
    status->addWidget(status_label_, 1);

    // 로딩 진행률
    load_progress_label_ = new QLabel();
    load_progress_label_->setStyleSheet("QLabel { padding: 0 8px; color: #aaa; }");
    status->addPermanentWidget(load_progress_label_);

    // 보안 표시기
    security_indicator_ = new QLabel("🟢 보안");
    security_indicator_->setStyleSheet(
        "QLabel { color: #4CAF50; font-weight: bold; padding: 2px 8px; }");
    status->addPermanentWidget(security_indicator_);
}

void MainWindow::setupCentralWidget() {
    auto* central = new QWidget(this);
    auto* main_layout = new QVBoxLayout(central);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    // 탭 바
    tab_bar_ = new TabBar(this);
    main_layout->addWidget(tab_bar_);
    connect(tab_bar_, &TabBar::tabSelected, this, &MainWindow::onTabChanged);
    connect(tab_bar_, &TabBar::newTabRequested, this, [this]() { openNewTab(); });
    connect(tab_bar_, &TabBar::tabCloseRequested, this, &MainWindow::closeTab);

    // 메인 스플리터 (콘텐츠 + 사이드 패널)
    main_splitter_ = new QSplitter(Qt::Horizontal, this);

    // 페이지 스택
    page_stack_ = new QStackedWidget();
    page_stack_->setStyleSheet("QStackedWidget { background: #1a1a1f; }");
    main_splitter_->addWidget(page_stack_);

    // 보안 패널 (오른쪽 사이드)
    security_panel_ = new SecurityPanel(this);
    security_panel_->setVisible(false);
    main_splitter_->addWidget(security_panel_);

    // 스플리터 비율 설정
    main_splitter_->setStretchFactor(0, 3);
    main_splitter_->setStretchFactor(1, 1);

    main_layout->addWidget(main_splitter_, 1);

    // 개발자 도구 (하단)
    dev_tools_panel_ = new DevToolsPanel(this);
    dev_tools_panel_->setVisible(false);
    main_layout->addWidget(dev_tools_panel_);

    setCentralWidget(central);

    // 기본 탭 열기
    openNewTab();
}

void MainWindow::setupShortcuts() {
    // Alt+← 뒤로 가기
    auto* back_shortcut = new QShortcut(QKeySequence("Alt+Left"), this);
    connect(back_shortcut, &QShortcut::activated, this, &MainWindow::goBack);

    // Alt+→ 앞으로 가기
    auto* forward_shortcut = new QShortcut(QKeySequence("Alt+Right"), this);
    connect(forward_shortcut, &QShortcut::activated, this, &MainWindow::goForward);

    // Ctrl+L 주소 바 포커스
    auto* address_shortcut = new QShortcut(QKeySequence("Ctrl+L"), this);
    connect(address_shortcut, &QShortcut::activated, this, [this]() {
        if (address_bar_) {
            address_bar_->setFocus();
            address_bar_->selectAll();
        }
    });

    // Ctrl+Tab 다음 탭
    auto* next_tab = new QShortcut(QKeySequence("Ctrl+Tab"), this);
    connect(next_tab, &QShortcut::activated, this, [this]() {
        if (tab_bar_ && tabCount() > 1) {
            int next = (currentTabIndex() + 1) % tabCount();
            tab_bar_->setCurrentTab(next);
        }
    });

    // Ctrl+Shift+Tab 이전 탭
    auto* prev_tab = new QShortcut(QKeySequence("Ctrl+Shift+Tab"), this);
    connect(prev_tab, &QShortcut::activated, this, [this]() {
        if (tab_bar_ && tabCount() > 1) {
            int prev = (currentTabIndex() - 1 + tabCount()) % tabCount();
            tab_bar_->setCurrentTab(prev);
        }
    });
}

void MainWindow::loadSettings() {
    // 윈도우 위치/크기 복원
    if (settings_.contains("geometry")) {
        restoreGeometry(settings_.value("geometry").toByteArray());
    }
    if (settings_.contains("windowState")) {
        restoreState(settings_.value("windowState").toByteArray());
    }
}

void MainWindow::saveSettings() {
    settings_.setValue("geometry", saveGeometry());
    settings_.setValue("windowState", saveState());
}

} // namespace ordinal::ui
