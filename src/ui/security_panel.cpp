/**
 * @file security_panel.cpp
 * @brief 보안 패널 위젯 구현
 * 
 * 실시간 위협 알림 목록, 보안 점수 게이지,
 * 트래커 차단 카운터, 인증서 정보 카드,
 * 페이지별 보안 요약 등 보안 정보를 종합적으로 표시합니다.
 */

#include "security_panel.h"
#include "main_window.h" // SecurityStatus 열거형

#include <QFont>
#include <QPainterPath>
#include <cmath>
#include <algorithm>

namespace ordinal::ui {

// ============================================================
// SecurityGauge 구현
// ============================================================

SecurityGauge::SecurityGauge(QWidget* parent) : QWidget(parent) {
    setFixedSize(120, 120);

    // 애니메이션 타이머
    anim_timer_ = new QTimer(this);
    anim_timer_->setInterval(16); // ~60fps
    connect(anim_timer_, &QTimer::timeout, this, [this]() {
        if (display_score_ < score_) {
            display_score_ = std::min(display_score_ + 2, score_);
            update();
        } else if (display_score_ > score_) {
            display_score_ = std::max(display_score_ - 2, score_);
            update();
        } else {
            anim_timer_->stop();
        }
    });
}

void SecurityGauge::setScore(int score) {
    score_ = std::clamp(score, 0, 100);
    if (animated_) {
        anim_timer_->start();
    } else {
        display_score_ = score_;
        update();
    }
}

void SecurityGauge::setAnimated(bool animated) {
    animated_ = animated;
}

void SecurityGauge::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();
    int side = std::min(w, h);
    int pen_width = 8;
    QRectF arc_rect(pen_width / 2.0, pen_width / 2.0,
                     side - pen_width, side - pen_width);

    // 배경 호 (회색)
    QPen bg_pen(QColor(60, 60, 70), pen_width);
    bg_pen.setCapStyle(Qt::RoundCap);
    painter.setPen(bg_pen);
    painter.drawArc(arc_rect, 225 * 16, -270 * 16);

    // 전경 호 (점수에 따른 색상)
    QColor fg_color = scoreColor(display_score_);
    QPen fg_pen(fg_color, pen_width);
    fg_pen.setCapStyle(Qt::RoundCap);
    painter.setPen(fg_pen);

    double span = -270.0 * (display_score_ / 100.0);
    painter.drawArc(arc_rect, 225 * 16, static_cast<int>(span * 16));

    // 중앙 텍스트 (점수)
    painter.setPen(fg_color);
    QFont score_font("SF Pro Display", 24, QFont::Bold);
    painter.setFont(score_font);
    painter.drawText(rect(), Qt::AlignCenter, QString::number(display_score_));

    // 하단 "점" 텍스트
    painter.setPen(QColor(150, 150, 160));
    QFont unit_font("SF Pro Display", 10);
    painter.setFont(unit_font);
    QRect text_rect = rect();
    text_rect.setTop(text_rect.center().y() + 15);
    painter.drawText(text_rect, Qt::AlignHCenter | Qt::AlignTop, "점");
}

QColor SecurityGauge::scoreColor(int score) const {
    if (score >= 80) return QColor(76, 175, 80);   // 녹색 — 안전
    if (score >= 60) return QColor(255, 193, 7);    // 노란색 — 주의
    if (score >= 40) return QColor(255, 152, 0);    // 주황색 — 경고
    return QColor(244, 67, 54);                      // 빨간색 — 위험
}

// ============================================================
// SecurityPanel 구현
// ============================================================

SecurityPanel::SecurityPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(280);
    setMaximumWidth(400);
    setStyleSheet(
        "QWidget { background: #1e1e26; color: #ddd; }"
        "QGroupBox {"
        "  border: 1px solid #3a3a45;"
        "  border-radius: 6px;"
        "  margin-top: 10px;"
        "  padding-top: 14px;"
        "  font-weight: bold;"
        "  font-size: 12px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  left: 12px;"
        "  padding: 0 6px;"
        "  color: #aaa;"
        "}"
    );

    setupUi();
}

void SecurityPanel::setupUi() {
    main_layout_ = new QVBoxLayout(this);
    main_layout_->setContentsMargins(0, 0, 0, 0);

    // 스크롤 영역
    scroll_area_ = new QScrollArea(this);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_area_->setStyleSheet(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical {"
        "  background: #1e1e26; width: 6px; margin: 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #444; border-radius: 3px; min-height: 20px;"
        "}"
    );

    content_widget_ = new QWidget();
    auto* content_layout = new QVBoxLayout(content_widget_);
    content_layout->setContentsMargins(12, 8, 12, 8);
    content_layout->setSpacing(12);

    // 패널 제목
    auto* title_label = new QLabel("🛡 보안 패널", content_widget_);
    title_label->setStyleSheet(
        "QLabel { font-size: 16px; font-weight: bold; color: #fff; padding: 4px 0; }");
    content_layout->addWidget(title_label);

    // 각 섹션 구성
    setupScoreSection();
    content_layout->addWidget(security_gauge_, 0, Qt::AlignHCenter);
    content_layout->addWidget(score_label_, 0, Qt::AlignHCenter);
    content_layout->addWidget(score_description_, 0, Qt::AlignHCenter);

    setupSummarySection();
    content_layout->addWidget(summary_group_);

    setupAlertSection();
    content_layout->addWidget(alert_group_);

    setupTrackerSection();
    content_layout->addWidget(tracker_group_);

    setupCertSection();
    content_layout->addWidget(cert_group_);

    content_layout->addStretch();

    scroll_area_->setWidget(content_widget_);
    main_layout_->addWidget(scroll_area_);
}

void SecurityPanel::setupScoreSection() {
    // 보안 점수 게이지
    security_gauge_ = new SecurityGauge(content_widget_);

    // 점수 라벨
    score_label_ = new QLabel("보안 점수", content_widget_);
    score_label_->setStyleSheet(
        "QLabel { font-size: 13px; color: #aaa; font-weight: bold; }");
    score_label_->setAlignment(Qt::AlignCenter);

    // 점수 설명
    score_description_ = new QLabel("안전한 연결입니다", content_widget_);
    score_description_->setStyleSheet("QLabel { font-size: 11px; color: #888; }");
    score_description_->setAlignment(Qt::AlignCenter);
}

void SecurityPanel::setupAlertSection() {
    alert_group_ = new QGroupBox("⚠ 위협 알림", content_widget_);
    auto* layout = new QVBoxLayout(alert_group_);
    layout->setSpacing(6);

    // 알림 수 라벨
    alert_count_label_ = new QLabel("위협 감지: 0건", alert_group_);
    alert_count_label_->setStyleSheet("QLabel { font-size: 11px; color: #888; }");
    layout->addWidget(alert_count_label_);

    // 알림 테이블
    alert_table_ = new QTableWidget(0, 3, alert_group_);
    alert_table_->setHorizontalHeaderLabels({"심각도", "위협", "시각"});
    alert_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    alert_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    alert_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    alert_table_->setAlternatingRowColors(true);
    alert_table_->setShowGrid(false);
    alert_table_->verticalHeader()->setVisible(false);
    alert_table_->setMaximumHeight(200);

    // 열 너비 설정
    auto* header = alert_table_->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Fixed);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::Fixed);
    alert_table_->setColumnWidth(0, 60);
    alert_table_->setColumnWidth(2, 50);

    alert_table_->setStyleSheet(
        "QTableWidget {"
        "  background: #22222c;"
        "  border: none;"
        "  font-size: 11px;"
        "}"
        "QTableWidget::item {"
        "  padding: 4px;"
        "  border-bottom: 1px solid #2a2a35;"
        "}"
        "QTableWidget::item:selected {"
        "  background: #3a3a50;"
        "}"
        "QHeaderView::section {"
        "  background: #2a2a35;"
        "  color: #999;"
        "  font-size: 10px;"
        "  font-weight: bold;"
        "  padding: 4px;"
        "  border: none;"
        "  border-bottom: 1px solid #444;"
        "}"
    );

    // 테이블 클릭 시그널
    connect(alert_table_, &QTableWidget::cellClicked, this, [this](int row, int /*col*/) {
        emit alertClicked(row);
    });

    layout->addWidget(alert_table_);
}

void SecurityPanel::setupTrackerSection() {
    tracker_group_ = new QGroupBox("🚫 트래커 차단", content_widget_);
    auto* layout = new QVBoxLayout(tracker_group_);
    layout->setSpacing(6);

    // 차단 수 라벨
    tracker_count_label_ = new QLabel("차단된 트래커: 0개", tracker_group_);
    tracker_count_label_->setStyleSheet(
        "QLabel { font-size: 13px; color: #4CAF50; font-weight: bold; }");
    layout->addWidget(tracker_count_label_);

    // 트래커 트리
    tracker_tree_ = new QTreeWidget(tracker_group_);
    tracker_tree_->setHeaderLabels({"이름", "도메인"});
    tracker_tree_->setMaximumHeight(160);
    tracker_tree_->setAlternatingRowColors(true);
    tracker_tree_->setRootIsDecorated(false);
    tracker_tree_->setStyleSheet(
        "QTreeWidget {"
        "  background: #22222c;"
        "  border: none;"
        "  font-size: 11px;"
        "}"
        "QTreeWidget::item {"
        "  padding: 3px;"
        "}"
        "QTreeWidget::item:selected {"
        "  background: #3a3a50;"
        "}"
        "QHeaderView::section {"
        "  background: #2a2a35;"
        "  color: #999;"
        "  font-size: 10px;"
        "  font-weight: bold;"
        "  padding: 3px;"
        "  border: none;"
        "}"
    );

    layout->addWidget(tracker_tree_);
}

void SecurityPanel::setupCertSection() {
    cert_group_ = new QGroupBox("🔒 인증서 정보", content_widget_);
    auto* layout = new QVBoxLayout(cert_group_);
    layout->setSpacing(4);

    auto make_label = [this](const QString& text) -> QLabel* {
        auto* label = new QLabel(text, cert_group_);
        label->setStyleSheet("QLabel { font-size: 11px; color: #bbb; padding: 2px 0; }");
        label->setWordWrap(true);
        return label;
    };

    cert_subject_label_ = make_label("주체: —");
    cert_issuer_label_ = make_label("발급 기관: —");
    cert_validity_label_ = make_label("유효 기간: —");
    cert_protocol_label_ = make_label("프로토콜: —");
    cert_fingerprint_label_ = make_label("SHA-256: —");

    layout->addWidget(cert_subject_label_);
    layout->addWidget(cert_issuer_label_);
    layout->addWidget(cert_validity_label_);
    layout->addWidget(cert_protocol_label_);
    layout->addWidget(cert_fingerprint_label_);
}

void SecurityPanel::setupSummarySection() {
    summary_group_ = new QGroupBox("📊 페이지 요약", content_widget_);
    auto* layout = new QVBoxLayout(summary_group_);
    layout->setSpacing(4);

    summary_url_label_ = new QLabel("URL: —", summary_group_);
    summary_url_label_->setStyleSheet("QLabel { font-size: 11px; color: #888; }");
    summary_url_label_->setWordWrap(true);
    layout->addWidget(summary_url_label_);

    summary_score_label_ = new QLabel("점수: —", summary_group_);
    summary_score_label_->setStyleSheet("QLabel { font-size: 11px; color: #bbb; }");
    layout->addWidget(summary_score_label_);

    summary_threats_label_ = new QLabel("위협: 0건", summary_group_);
    summary_threats_label_->setStyleSheet("QLabel { font-size: 11px; color: #bbb; }");
    layout->addWidget(summary_threats_label_);

    summary_trackers_label_ = new QLabel("트래커: 0개", summary_group_);
    summary_trackers_label_->setStyleSheet("QLabel { font-size: 11px; color: #bbb; }");
    layout->addWidget(summary_trackers_label_);

    // 재검사 버튼
    rescan_button_ = new QPushButton("🔄 보안 재검사", summary_group_);
    rescan_button_->setCursor(Qt::PointingHandCursor);
    rescan_button_->setStyleSheet(
        "QPushButton {"
        "  background: #3a3a50;"
        "  color: #ddd;"
        "  border: 1px solid #4682dc;"
        "  border-radius: 4px;"
        "  padding: 6px 12px;"
        "  font-size: 11px;"
        "}"
        "QPushButton:hover { background: #4682dc; color: #fff; }"
    );
    connect(rescan_button_, &QPushButton::clicked, this, [this]() {
        emit rescanRequested();
    });
    layout->addWidget(rescan_button_);
}

// ============================================================
// 공개 API
// ============================================================

void SecurityPanel::updateStatus(SecurityStatus status, const QString& message) {
    switch (status) {
        case SecurityStatus::Secure:
            setSecurityScore(95);
            if (score_description_) {
                score_description_->setText(message.isEmpty() ? "안전한 연결입니다" : message);
                score_description_->setStyleSheet("QLabel { font-size: 11px; color: #4CAF50; }");
            }
            break;
        case SecurityStatus::Warning:
            setSecurityScore(60);
            if (score_description_) {
                score_description_->setText(message.isEmpty() ? "일부 보안 문제가 있습니다" : message);
                score_description_->setStyleSheet("QLabel { font-size: 11px; color: #FFC107; }");
            }
            break;
        case SecurityStatus::Danger:
            setSecurityScore(20);
            if (score_description_) {
                score_description_->setText(message.isEmpty() ? "위험한 사이트입니다!" : message);
                score_description_->setStyleSheet("QLabel { font-size: 11px; color: #F44336; }");
            }
            break;
    }
}

void SecurityPanel::setSecurityScore(int score) {
    if (security_gauge_) {
        security_gauge_->setScore(score);
    }
}

void SecurityPanel::addThreatAlert(const ThreatAlert& alert) {
    alerts_.push_back(alert);

    // 심각도 기준 내림차순 정렬
    std::sort(alerts_.begin(), alerts_.end(),
        [](const ThreatAlert& a, const ThreatAlert& b) {
            return static_cast<int>(a.severity) > static_cast<int>(b.severity);
        });

    updateAlertTable();

    if (alert_count_label_) {
        alert_count_label_->setText(
            QString("위협 감지: %1건").arg(alerts_.size()));
    }
}

void SecurityPanel::clearAlerts() {
    alerts_.clear();
    if (alert_table_) {
        alert_table_->setRowCount(0);
    }
    if (alert_count_label_) {
        alert_count_label_->setText("위협 감지: 0건");
    }
}

void SecurityPanel::setBlockedTrackers(int count) {
    blocked_tracker_count_ = count;
    if (tracker_count_label_) {
        tracker_count_label_->setText(
            QString("차단된 트래커: %1개").arg(count));
    }
}

void SecurityPanel::addBlockedTracker(const QString& name, const QString& domain) {
    blocked_tracker_count_++;
    if (tracker_count_label_) {
        tracker_count_label_->setText(
            QString("차단된 트래커: %1개").arg(blocked_tracker_count_));
    }

    if (tracker_tree_) {
        auto* item = new QTreeWidgetItem(tracker_tree_);
        item->setText(0, name);
        item->setText(1, domain);
        item->setForeground(0, QColor(200, 200, 210));
        item->setForeground(1, QColor(150, 150, 160));
    }
}

void SecurityPanel::setCertificateInfo(const CertificateInfo& cert) {
    if (cert_subject_label_) {
        QString subject_text = cert.subject;
        if (cert.is_ev) subject_text += " (EV)";
        cert_subject_label_->setText("주체: " + subject_text);
    }
    if (cert_issuer_label_) {
        cert_issuer_label_->setText("발급 기관: " + cert.issuer);
    }
    if (cert_validity_label_) {
        QString validity = cert.valid_from.toString("yyyy-MM-dd") + " ~ " +
                          cert.valid_until.toString("yyyy-MM-dd");
        // 만료 확인
        if (cert.valid_until < QDateTime::currentDateTime()) {
            validity += " ⚠ 만료됨";
            cert_validity_label_->setStyleSheet(
                "QLabel { font-size: 11px; color: #F44336; padding: 2px 0; }");
        } else {
            cert_validity_label_->setStyleSheet(
                "QLabel { font-size: 11px; color: #bbb; padding: 2px 0; }");
        }
        cert_validity_label_->setText("유효 기간: " + validity);
    }
    if (cert_protocol_label_) {
        cert_protocol_label_->setText(
            QString("프로토콜: %1 | %2 | %3bit")
                .arg(cert.protocol, cert.cipher_suite)
                .arg(cert.key_bits));
    }
    if (cert_fingerprint_label_) {
        // 긴 지문 줄임
        QString fp = cert.fingerprint_sha256;
        if (fp.length() > 32) {
            fp = fp.left(16) + "..." + fp.right(16);
        }
        cert_fingerprint_label_->setText("SHA-256: " + fp);
        cert_fingerprint_label_->setToolTip(cert.fingerprint_sha256);
    }
}

void SecurityPanel::setPageSummary(const QString& url, int score, int threats, int trackers) {
    if (summary_url_label_) {
        // URL 줄임 표시
        QString display_url = url;
        if (display_url.length() > 50) {
            display_url = display_url.left(47) + "...";
        }
        summary_url_label_->setText("URL: " + display_url);
        summary_url_label_->setToolTip(url);
    }
    if (summary_score_label_) {
        summary_score_label_->setText(QString("점수: %1/100").arg(score));
    }
    if (summary_threats_label_) {
        summary_threats_label_->setText(QString("위협: %1건").arg(threats));
        if (threats > 0) {
            summary_threats_label_->setStyleSheet(
                "QLabel { font-size: 11px; color: #F44336; }");
        } else {
            summary_threats_label_->setStyleSheet(
                "QLabel { font-size: 11px; color: #4CAF50; }");
        }
    }
    if (summary_trackers_label_) {
        summary_trackers_label_->setText(QString("트래커: %1개 차단됨").arg(trackers));
    }
}

// ============================================================
// 내부 메서드
// ============================================================

void SecurityPanel::updateAlertTable() {
    if (!alert_table_) return;

    alert_table_->setRowCount(static_cast<int>(alerts_.size()));

    for (int i = 0; i < static_cast<int>(alerts_.size()); ++i) {
        const auto& alert = alerts_[i];

        // 심각도 셀
        auto* severity_item = new QTableWidgetItem(
            severityIcon(alert.severity) + " " + severityText(alert.severity));
        severity_item->setForeground(severityColor(alert.severity));
        alert_table_->setItem(i, 0, severity_item);

        // 위협 제목 셀
        auto* title_item = new QTableWidgetItem(alert.title);
        title_item->setToolTip(alert.description);
        title_item->setForeground(QColor(200, 200, 210));
        alert_table_->setItem(i, 1, title_item);

        // 시각 셀
        auto* time_item = new QTableWidgetItem(
            alert.timestamp.toString("HH:mm"));
        time_item->setForeground(QColor(150, 150, 160));
        alert_table_->setItem(i, 2, time_item);
    }
}

QString SecurityPanel::severityText(ThreatSeverity severity) const {
    switch (severity) {
        case ThreatSeverity::Info:     return "정보";
        case ThreatSeverity::Low:      return "낮음";
        case ThreatSeverity::Medium:   return "보통";
        case ThreatSeverity::High:     return "높음";
        case ThreatSeverity::Critical: return "심각";
    }
    return "알 수 없음";
}

QColor SecurityPanel::severityColor(ThreatSeverity severity) const {
    switch (severity) {
        case ThreatSeverity::Info:     return QColor(100, 149, 237);
        case ThreatSeverity::Low:      return QColor(76, 175, 80);
        case ThreatSeverity::Medium:   return QColor(255, 193, 7);
        case ThreatSeverity::High:     return QColor(255, 152, 0);
        case ThreatSeverity::Critical: return QColor(244, 67, 54);
    }
    return QColor(150, 150, 160);
}

QString SecurityPanel::severityIcon(ThreatSeverity severity) const {
    switch (severity) {
        case ThreatSeverity::Info:     return "ℹ";
        case ThreatSeverity::Low:      return "🔵";
        case ThreatSeverity::Medium:   return "🟡";
        case ThreatSeverity::High:     return "🟠";
        case ThreatSeverity::Critical: return "🔴";
    }
    return "❓";
}

} // namespace ordinal::ui
