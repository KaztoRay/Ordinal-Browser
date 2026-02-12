#pragma once

/**
 * @file tab_bar.h
 * @brief 커스텀 탭 바
 * 
 * QWidget 기반 커스텀 탭 바.
 * 새 탭/닫기 버튼, 드래그 앤 드롭 재정렬, 파비콘 표시,
 * 로딩 스피너 애니메이션, 우클릭 컨텍스트 메뉴를 제공합니다.
 */

#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QPixmap>
#include <QMenu>
#include <QPropertyAnimation>
#include <QPainter>

#include <vector>
#include <memory>
#include <functional>

namespace ordinal::ui {

/**
 * @brief 개별 탭 아이템 위젯
 * 
 * 파비콘, 제목, 닫기 버튼, 로딩 스피너를 포함하는
 * 단일 탭을 나타내는 위젯입니다.
 */
class TabItem : public QWidget {
    Q_OBJECT

public:
    explicit TabItem(const QString& title, int index, QWidget* parent = nullptr);
    ~TabItem() override = default;

    // 속성 접근자
    [[nodiscard]] int index() const { return index_; }
    void setIndex(int idx) { index_ = idx; }

    [[nodiscard]] QString title() const { return title_; }
    void setTitle(const QString& title);

    [[nodiscard]] bool isActive() const { return active_; }
    void setActive(bool active);

    [[nodiscard]] bool isPinned() const { return pinned_; }
    void setPinned(bool pinned);

    [[nodiscard]] bool isLoading() const { return loading_; }
    void setLoading(bool loading);

    // 파비콘 설정
    void setFavicon(const QPixmap& favicon);

signals:
    void clicked(int index);
    void closeRequested(int index);
    void contextMenuRequested(int index, const QPoint& pos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private slots:
    void onSpinnerTick();

private:
    void updateStyle();

    int index_;
    QString title_;
    bool active_{false};
    bool pinned_{false};
    bool loading_{false};
    bool hovered_{false};

    // 파비콘
    QPixmap favicon_;
    bool has_favicon_{false};

    // 로딩 스피너
    QTimer* spinner_timer_{nullptr};
    int spinner_angle_{0};

    // 드래그 시작 좌표
    QPoint drag_start_pos_;

    // 내부 위젯
    QLabel* favicon_label_{nullptr};
    QLabel* title_label_{nullptr};
    QPushButton* close_button_{nullptr};
};

// ============================================================

/**
 * @brief 커스텀 탭 바 위젯
 * 
 * 브라우저 상단 탭 바. 탭 추가/제거/재정렬,
 * 드래그 앤 드롭, 컨텍스트 메뉴를 지원합니다.
 */
class TabBar : public QWidget {
    Q_OBJECT

public:
    explicit TabBar(QWidget* parent = nullptr);
    ~TabBar() override = default;

    // ============================
    // 탭 관리
    // ============================

    /**
     * @brief 새 탭 추가
     * @param title 탭 제목
     * @return 새 탭 인덱스
     */
    int addNewTab(const QString& title = "새 탭");

    /**
     * @brief 탭 제거
     * @param index 제거할 인덱스
     */
    void removeTab(int index);

    /**
     * @brief 탭 제목 설정
     */
    void setTabTitle(int index, const QString& title);

    /**
     * @brief 탭 파비콘 설정
     */
    void setTabFavicon(int index, const QPixmap& favicon);

    /**
     * @brief 탭 로딩 상태 설정
     */
    void setTabLoading(int index, bool loading);

    /**
     * @brief 현재 탭 설정
     */
    void setCurrentTab(int index);

    /**
     * @brief 현재 탭 인덱스
     */
    [[nodiscard]] int currentTab() const { return current_index_; }

    /**
     * @brief 탭 수
     */
    [[nodiscard]] int count() const { return static_cast<int>(tabs_.size()); }

    /**
     * @brief 탭을 다른 위치로 이동 (드래그 앤 드롭)
     */
    void moveTab(int from, int to);

    /**
     * @brief 탭 고정/해제
     */
    void pinTab(int index);

    /**
     * @brief 탭 복제
     */
    void duplicateTab(int index);

    /**
     * @brief 다른 탭 모두 닫기
     */
    void closeOtherTabs(int except_index);

    /**
     * @brief 오른쪽 탭 모두 닫기
     */
    void closeTabsToRight(int index);

signals:
    void tabSelected(int index);
    void newTabRequested();
    void tabCloseRequested(int index);
    void tabMoved(int from, int to);
    void tabDuplicated(int index);
    void tabPinned(int index, bool pinned);

private slots:
    void onTabClicked(int index);
    void onTabCloseClicked(int index);
    void onTabContextMenu(int index, const QPoint& pos);

private:
    void rebuildLayout();
    void updateTabIndices();
    void showContextMenu(int index, const QPoint& pos);

    // 탭 컨테이너
    QHBoxLayout* tabs_layout_{nullptr};
    QWidget* tabs_container_{nullptr};
    QPushButton* new_tab_button_{nullptr};

    // 탭 목록
    std::vector<TabItem*> tabs_;
    int current_index_{-1};

    // 드래그 상태
    int drag_source_index_{-1};
    bool dragging_{false};
};

} // namespace ordinal::ui
