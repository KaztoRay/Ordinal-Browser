/**
 * @file tab_bar.cpp
 * @brief 커스텀 탭 바 구현
 * 
 * 드래그 앤 드롭 재정렬, 파비콘, 로딩 스피너,
 * 컨텍스트 메뉴 (닫기/다른 탭 닫기/복제/고정) 등
 * 브라우저 탭 바의 전체 기능을 구현합니다.
 */

#include "tab_bar.h"

#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QScrollArea>
#include <QPainterPath>
#include <QContextMenuEvent>
#include <QStyleOption>
#include <algorithm>
#include <cmath>

namespace ordinal::ui {

// ============================================================
// TabItem 구현
// ============================================================

TabItem::TabItem(const QString& title, int index, QWidget* parent)
    : QWidget(parent), index_(index), title_(title) {
    
    // 고정 높이, 가변 너비
    setFixedHeight(36);
    setMinimumWidth(60);
    setMaximumWidth(240);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMouseTracking(true);
    setAcceptDrops(true);

    // 내부 레이아웃 구성
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 4, 0);
    layout->setSpacing(6);

    // 파비콘 라벨 (16x16)
    favicon_label_ = new QLabel(this);
    favicon_label_->setFixedSize(16, 16);
    favicon_label_->setScaledContents(true);
    favicon_label_->setStyleSheet("QLabel { background: transparent; }");
    layout->addWidget(favicon_label_);

    // 제목 라벨
    title_label_ = new QLabel(title, this);
    title_label_->setStyleSheet("QLabel { color: #ccc; background: transparent; font-size: 12px; }");
    title_label_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    title_label_->setMinimumWidth(20);
    layout->addWidget(title_label_, 1);

    // 닫기 버튼
    close_button_ = new QPushButton("✕", this);
    close_button_->setFixedSize(18, 18);
    close_button_->setStyleSheet(
        "QPushButton { color: #888; background: transparent; border: none; font-size: 10px; border-radius: 9px; }"
        "QPushButton:hover { color: #fff; background: #e74c3c; }"
    );
    close_button_->setCursor(Qt::PointingHandCursor);
    close_button_->setVisible(false); // 호버 시에만 표시
    connect(close_button_, &QPushButton::clicked, this, [this]() {
        emit closeRequested(index_);
    });
    layout->addWidget(close_button_);

    // 로딩 스피너 타이머
    spinner_timer_ = new QTimer(this);
    spinner_timer_->setInterval(50); // 50ms마다 회전
    connect(spinner_timer_, &QTimer::timeout, this, &TabItem::onSpinnerTick);

    updateStyle();
}

void TabItem::setTitle(const QString& title) {
    title_ = title;
    if (title_label_) {
        // 고정 탭은 제목 숨김
        if (pinned_) {
            title_label_->clear();
        } else {
            // 긴 제목 줄임 처리
            QFontMetrics fm(title_label_->font());
            QString elided = fm.elidedText(title, Qt::ElideRight, title_label_->width());
            title_label_->setText(elided);
            title_label_->setToolTip(title);
        }
    }
}

void TabItem::setActive(bool active) {
    active_ = active;
    updateStyle();
    update();
}

void TabItem::setPinned(bool pinned) {
    pinned_ = pinned;
    if (pinned) {
        setMaximumWidth(46);
        title_label_->setVisible(false);
        close_button_->setVisible(false);
    } else {
        setMaximumWidth(240);
        title_label_->setVisible(true);
    }
    updateStyle();
}

void TabItem::setLoading(bool loading) {
    loading_ = loading;
    if (loading) {
        spinner_angle_ = 0;
        spinner_timer_->start();
        favicon_label_->clear();
    } else {
        spinner_timer_->stop();
        // 파비콘 복원
        if (has_favicon_) {
            favicon_label_->setPixmap(favicon_);
        }
    }
    update();
}

void TabItem::setFavicon(const QPixmap& favicon) {
    favicon_ = favicon.scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    has_favicon_ = true;
    if (!loading_) {
        favicon_label_->setPixmap(favicon_);
    }
}

void TabItem::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 배경 색상 결정
    QColor bg_color;
    if (active_) {
        bg_color = QColor(45, 45, 55);
    } else if (hovered_) {
        bg_color = QColor(38, 38, 46);
    } else {
        bg_color = QColor(30, 30, 38);
    }

    // 둥근 상단 모서리 배경
    QPainterPath path;
    path.moveTo(0, height());
    path.lineTo(0, 6);
    path.quadTo(0, 0, 6, 0);
    path.lineTo(width() - 6, 0);
    path.quadTo(width(), 0, width(), 6);
    path.lineTo(width(), height());
    path.closeSubpath();

    painter.fillPath(path, bg_color);

    // 활성 탭 하단 강조선
    if (active_) {
        painter.setPen(QPen(QColor(70, 130, 220), 2));
        painter.drawLine(4, height() - 1, width() - 4, height() - 1);
    }

    // 로딩 스피너 그리기
    if (loading_) {
        painter.save();
        QPoint center = favicon_label_->geometry().center();
        painter.translate(center);
        painter.rotate(spinner_angle_);

        QPen spinner_pen(QColor(70, 130, 220), 2);
        spinner_pen.setCapStyle(Qt::RoundCap);
        painter.setPen(spinner_pen);

        // 호 그리기 (스피너 효과)
        QRectF arc_rect(-6, -6, 12, 12);
        painter.drawArc(arc_rect, 0, 270 * 16);

        painter.restore();
    }
}

void TabItem::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        drag_start_pos_ = event->pos();
        emit clicked(index_);
    } else if (event->button() == Qt::MiddleButton) {
        // 중간 클릭으로 탭 닫기
        emit closeRequested(index_);
    }
    QWidget::mousePressEvent(event);
}

void TabItem::mouseReleaseEvent(QMouseEvent* event) {
    QWidget::mouseReleaseEvent(event);
}

void TabItem::mouseMoveEvent(QMouseEvent* event) {
    // 드래그 앤 드롭 시작 감지
    if (event->buttons() & Qt::LeftButton) {
        int distance = (event->pos() - drag_start_pos_).manhattanLength();
        if (distance >= QApplication::startDragDistance()) {
            // 드래그 시작
            auto* drag = new QDrag(this);
            auto* mime = new QMimeData();
            mime->setData("application/x-ordinal-tab", QByteArray::number(index_));
            drag->setMimeData(mime);

            // 드래그 썸네일 생성
            QPixmap pixmap(size());
            pixmap.fill(Qt::transparent);
            render(&pixmap);
            drag->setPixmap(pixmap);
            drag->setHotSpot(event->pos());

            drag->exec(Qt::MoveAction);
        }
    }
    QWidget::mouseMoveEvent(event);
}

void TabItem::enterEvent(QEnterEvent* /*event*/) {
    hovered_ = true;
    if (!pinned_) {
        close_button_->setVisible(true);
    }
    update();
}

void TabItem::leaveEvent(QEvent* /*event*/) {
    hovered_ = false;
    if (!active_) {
        close_button_->setVisible(false);
    }
    update();
}

void TabItem::contextMenuEvent(QContextMenuEvent* event) {
    emit contextMenuRequested(index_, event->globalPos());
}

void TabItem::onSpinnerTick() {
    spinner_angle_ = (spinner_angle_ + 15) % 360;
    update();
}

void TabItem::updateStyle() {
    if (title_label_) {
        if (active_) {
            title_label_->setStyleSheet(
                "QLabel { color: #fff; background: transparent; font-size: 12px; font-weight: bold; }");
        } else {
            title_label_->setStyleSheet(
                "QLabel { color: #aaa; background: transparent; font-size: 12px; }");
        }
    }

    // 활성 탭은 닫기 버튼 항상 표시
    if (close_button_ && !pinned_) {
        close_button_->setVisible(active_ || hovered_);
    }

    setCursor(Qt::PointingHandCursor);
}

// ============================================================
// TabBar 구현
// ============================================================

TabBar::TabBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(38);
    setAcceptDrops(true);
    setStyleSheet("TabBar { background: #1a1a22; }");

    // 메인 레이아웃
    auto* main_layout = new QHBoxLayout(this);
    main_layout->setContentsMargins(4, 2, 4, 0);
    main_layout->setSpacing(0);

    // 스크롤 가능한 탭 컨테이너
    auto* scroll_area = new QScrollArea(this);
    scroll_area->setWidgetResizable(true);
    scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area->setFixedHeight(36);
    scroll_area->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
    );

    tabs_container_ = new QWidget();
    tabs_container_->setStyleSheet("background: transparent;");
    tabs_layout_ = new QHBoxLayout(tabs_container_);
    tabs_layout_->setContentsMargins(0, 0, 0, 0);
    tabs_layout_->setSpacing(1);
    tabs_layout_->addStretch(); // 오른쪽 여백

    scroll_area->setWidget(tabs_container_);
    main_layout->addWidget(scroll_area, 1);

    // 새 탭 버튼 (+)
    new_tab_button_ = new QPushButton("+", this);
    new_tab_button_->setFixedSize(28, 28);
    new_tab_button_->setCursor(Qt::PointingHandCursor);
    new_tab_button_->setStyleSheet(
        "QPushButton { color: #888; background: transparent; border: none; font-size: 18px; border-radius: 14px; }"
        "QPushButton:hover { color: #fff; background: #3a3a45; }"
    );
    new_tab_button_->setToolTip("새 탭 (Ctrl+T)");
    connect(new_tab_button_, &QPushButton::clicked, this, [this]() {
        emit newTabRequested();
    });
    main_layout->addWidget(new_tab_button_);
}

int TabBar::addNewTab(const QString& title) {
    int index = static_cast<int>(tabs_.size());

    auto* tab = new TabItem(title, index, tabs_container_);
    connect(tab, &TabItem::clicked, this, &TabBar::onTabClicked);
    connect(tab, &TabItem::closeRequested, this, &TabBar::onTabCloseClicked);
    connect(tab, &TabItem::contextMenuRequested, this, &TabBar::onTabContextMenu);

    tabs_.push_back(tab);

    // 레이아웃에 추가 (stretch 앞에)
    int insert_pos = tabs_layout_->count() - 1; // stretch 이전
    tabs_layout_->insertWidget(insert_pos, tab);

    // 새 탭을 활성화
    setCurrentTab(index);

    return index;
}

void TabBar::removeTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;

    // 탭 위젯 제거
    auto* tab = tabs_[index];
    tabs_layout_->removeWidget(tab);
    tabs_.erase(tabs_.begin() + index);
    tab->deleteLater();

    // 인덱스 재정렬
    updateTabIndices();

    // 현재 탭 인덱스 조정
    if (tabs_.empty()) {
        current_index_ = -1;
    } else if (current_index_ >= static_cast<int>(tabs_.size())) {
        setCurrentTab(static_cast<int>(tabs_.size()) - 1);
    } else if (current_index_ == index) {
        // 닫힌 탭이 현재 탭이면 이전 탭 또는 다음 탭으로 이동
        int new_index = (index > 0) ? index - 1 : 0;
        setCurrentTab(new_index);
    } else if (current_index_ > index) {
        current_index_--;
    }
}

void TabBar::setTabTitle(int index, const QString& title) {
    if (index >= 0 && index < static_cast<int>(tabs_.size())) {
        tabs_[index]->setTitle(title);
    }
}

void TabBar::setTabFavicon(int index, const QPixmap& favicon) {
    if (index >= 0 && index < static_cast<int>(tabs_.size())) {
        tabs_[index]->setFavicon(favicon);
    }
}

void TabBar::setTabLoading(int index, bool loading) {
    if (index >= 0 && index < static_cast<int>(tabs_.size())) {
        tabs_[index]->setLoading(loading);
    }
}

void TabBar::setCurrentTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
    if (index == current_index_) return;

    // 이전 탭 비활성화
    if (current_index_ >= 0 && current_index_ < static_cast<int>(tabs_.size())) {
        tabs_[current_index_]->setActive(false);
    }

    // 새 탭 활성화
    current_index_ = index;
    tabs_[current_index_]->setActive(true);

    emit tabSelected(current_index_);
}

void TabBar::moveTab(int from, int to) {
    if (from < 0 || from >= static_cast<int>(tabs_.size())) return;
    if (to < 0 || to >= static_cast<int>(tabs_.size())) return;
    if (from == to) return;

    // 탭 포인터 이동
    auto* tab = tabs_[from];
    tabs_.erase(tabs_.begin() + from);
    tabs_.insert(tabs_.begin() + to, tab);

    // 인덱스 업데이트 및 레이아웃 재구성
    updateTabIndices();
    rebuildLayout();

    // 현재 탭 인덱스 조정
    if (current_index_ == from) {
        current_index_ = to;
    } else if (from < current_index_ && to >= current_index_) {
        current_index_--;
    } else if (from > current_index_ && to <= current_index_) {
        current_index_++;
    }

    emit tabMoved(from, to);
}

void TabBar::pinTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;

    bool new_state = !tabs_[index]->isPinned();
    tabs_[index]->setPinned(new_state);

    // 고정 탭은 왼쪽으로 이동
    if (new_state) {
        // 고정 탭 영역의 마지막 위치 찾기
        int pin_end = 0;
        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            if (tabs_[i]->isPinned() && i != index) {
                pin_end = i + 1;
            }
        }
        if (index != pin_end) {
            moveTab(index, pin_end);
        }
    }

    emit tabPinned(index, new_state);
}

void TabBar::duplicateTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
    emit tabDuplicated(index);
}

void TabBar::closeOtherTabs(int except_index) {
    // 뒤에서부터 닫아야 인덱스가 꼬이지 않음
    for (int i = static_cast<int>(tabs_.size()) - 1; i >= 0; --i) {
        if (i != except_index && !tabs_[i]->isPinned()) {
            emit tabCloseRequested(i);
        }
    }
}

void TabBar::closeTabsToRight(int index) {
    for (int i = static_cast<int>(tabs_.size()) - 1; i > index; --i) {
        if (!tabs_[i]->isPinned()) {
            emit tabCloseRequested(i);
        }
    }
}

// ============================================================
// 슬롯
// ============================================================

void TabBar::onTabClicked(int index) {
    setCurrentTab(index);
}

void TabBar::onTabCloseClicked(int index) {
    emit tabCloseRequested(index);
}

void TabBar::onTabContextMenu(int index, const QPoint& pos) {
    showContextMenu(index, pos);
}

// ============================================================
// 내부 메서드
// ============================================================

void TabBar::rebuildLayout() {
    // 기존 위젯 모두 제거 (삭제하지 않음)
    while (tabs_layout_->count() > 0) {
        tabs_layout_->takeAt(0);
    }

    // 고정 탭 먼저 추가
    for (auto* tab : tabs_) {
        if (tab->isPinned()) {
            tabs_layout_->addWidget(tab);
        }
    }

    // 일반 탭 추가
    for (auto* tab : tabs_) {
        if (!tab->isPinned()) {
            tabs_layout_->addWidget(tab);
        }
    }

    // 스트레치 추가
    tabs_layout_->addStretch();
}

void TabBar::updateTabIndices() {
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        tabs_[i]->setIndex(i);
    }
}

void TabBar::showContextMenu(int index, const QPoint& pos) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background: #2d2d35; color: #ddd; border: 1px solid #444; padding: 4px; }"
        "QMenu::item { padding: 6px 24px; }"
        "QMenu::item:selected { background: #4682dc; }"
        "QMenu::separator { height: 1px; background: #444; margin: 4px 0; }"
    );

    // 새 탭
    menu.addAction("새 탭", this, [this]() {
        emit newTabRequested();
    });

    menu.addSeparator();

    // 탭 복제
    menu.addAction("탭 복제", this, [this, index]() {
        duplicateTab(index);
    });

    // 탭 고정/해제
    bool is_pinned = tabs_[index]->isPinned();
    menu.addAction(is_pinned ? "탭 고정 해제" : "탭 고정", this, [this, index]() {
        pinTab(index);
    });

    menu.addSeparator();

    // 닫기 옵션
    if (!tabs_[index]->isPinned()) {
        menu.addAction("탭 닫기", this, [this, index]() {
            emit tabCloseRequested(index);
        });
    }

    menu.addAction("다른 탭 모두 닫기", this, [this, index]() {
        closeOtherTabs(index);
    });

    menu.addAction("오른쪽 탭 모두 닫기", this, [this, index]() {
        closeTabsToRight(index);
    });

    menu.exec(pos);
}

} // namespace ordinal::ui
