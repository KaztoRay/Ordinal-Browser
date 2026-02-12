#pragma once

/**
 * @file main_window.h
 * @brief 메인 윈도우
 * 
 * Qt6 QMainWindow 기반 브라우저 메인 윈도우.
 * 메뉴 바, 툴바, 탭 스택, 상태 바, 보안 표시기를 포함합니다.
 */

#include <QMainWindow>
#include <QTabWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QAction>
#include <QCloseEvent>
#include <QStackedWidget>
#include <QSplitter>
#include <QTimer>
#include <QSettings>

#include <memory>
#include <vector>
#include <string>

namespace ordinal::ui {

// 전방 선언
class TabBar;
class AddressBar;
class SecurityPanel;
class DevToolsPanel;

/**
 * @brief 보안 상태 열거형
 */
enum class SecurityStatus {
    Secure,     ///< 🟢 안전 (유효한 HTTPS)
    Warning,    ///< 🟡 경고 (혼합 콘텐츠 등)
    Danger      ///< 🔴 위험 (피싱/악성 사이트)
};

/**
 * @brief 브라우저 메인 윈도우
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // ============================
    // 탭 관리
    // ============================

    /**
     * @brief 새 탭 열기
     * @param url 초기 URL (비어있으면 빈 탭)
     * @return 탭 인덱스
     */
    int openNewTab(const QString& url = QString());

    /**
     * @brief 현재 탭 닫기
     */
    void closeCurrentTab();

    /**
     * @brief 특정 인덱스의 탭 닫기
     */
    void closeTab(int index);

    /**
     * @brief 현재 탭 인덱스
     */
    [[nodiscard]] int currentTabIndex() const;

    /**
     * @brief 탭 수
     */
    [[nodiscard]] int tabCount() const;

    // ============================
    // 네비게이션
    // ============================

    /**
     * @brief URL로 이동
     */
    void navigateTo(const QString& url);

    /**
     * @brief 뒤로 가기
     */
    void goBack();

    /**
     * @brief 앞으로 가기
     */
    void goForward();

    /**
     * @brief 새로고침
     */
    void reload();

    /**
     * @brief 로딩 중지
     */
    void stopLoading();

    // ============================
    // 보안 상태
    // ============================

    /**
     * @brief 보안 상태 업데이트
     */
    void updateSecurityStatus(SecurityStatus status, const QString& message);

    /**
     * @brief 보안 패널 토글
     */
    void toggleSecurityPanel();

    /**
     * @brief 개발자 도구 토글
     */
    void toggleDevTools();

signals:
    /**
     * @brief 탭 변경 시그널
     */
    void tabChanged(int index);

    /**
     * @brief URL 변경 시그널
     */
    void urlChanged(const QString& url);

    /**
     * @brief 보안 상태 변경 시그널
     */
    void securityStatusChanged(SecurityStatus status);

    /**
     * @brief 페이지 로딩 진행률
     */
    void loadProgress(int percent);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTabChanged(int index);
    void onAddressEntered(const QString& url);
    void onLoadStarted();
    void onLoadFinished(bool success);
    void onLoadProgressUpdate(int percent);
    void onTitleChanged(const QString& title);

private:
    // UI 초기화 메서드
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupCentralWidget();
    void setupShortcuts();
    void loadSettings();
    void saveSettings();

    // 메뉴 액션 생성
    void createFileMenu();
    void createEditMenu();
    void createViewMenu();
    void createSecurityMenu();
    void createHelpMenu();

    // UI 컴포넌트
    TabBar* tab_bar_{nullptr};
    AddressBar* address_bar_{nullptr};
    SecurityPanel* security_panel_{nullptr};
    DevToolsPanel* dev_tools_panel_{nullptr};

    QToolBar* nav_toolbar_{nullptr};
    QToolBar* address_toolbar_{nullptr};
    QStackedWidget* page_stack_{nullptr};
    QSplitter* main_splitter_{nullptr};

    // 네비게이션 액션
    QAction* action_back_{nullptr};
    QAction* action_forward_{nullptr};
    QAction* action_reload_{nullptr};
    QAction* action_stop_{nullptr};
    QAction* action_new_tab_{nullptr};
    QAction* action_close_tab_{nullptr};
    QAction* action_dev_tools_{nullptr};
    QAction* action_security_panel_{nullptr};

    // 상태 바 위젯
    QLabel* status_label_{nullptr};
    QLabel* security_indicator_{nullptr};
    QLabel* load_progress_label_{nullptr};

    // 보안 상태
    SecurityStatus current_security_status_{SecurityStatus::Secure};

    // 설정
    QSettings settings_{"Ordinal", "Browser"};

    // 개발자 도구 표시 상태
    bool dev_tools_visible_{false};
    bool security_panel_visible_{false};
};

} // namespace ordinal::ui
