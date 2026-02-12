/**
 * @file dev_tools_panel.cpp
 * @brief 개발자 도구 패널 구현
 * 
 * 콘솔 (V8 eval), 네트워크 워터폴, 보안 스캔 결과,
 * DOM 요소 트리 등 개발자 도구의 전체 기능을 구현합니다.
 */

#include "dev_tools_panel.h"

#include <QApplication>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QTextBlock>
#include <QScrollBar>
#include <algorithm>
#include <cmath>

namespace ordinal::ui {

// ============================================================
// ConsoleTab 구현
// ============================================================

ConsoleTab::ConsoleTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 상단 툴바
    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(30);
    toolbar->setStyleSheet("QWidget { background: #252530; border-bottom: 1px solid #333; }");
    auto* toolbar_layout = new QHBoxLayout(toolbar);
    toolbar_layout->setContentsMargins(8, 2, 8, 2);
    toolbar_layout->setSpacing(8);

    // 로그 레벨 필터
    filter_combo_ = new QComboBox(toolbar);
    filter_combo_->addItems({"전체", "정보", "경고", "오류", "디버그"});
    filter_combo_->setStyleSheet(
        "QComboBox { background: #2d2d38; color: #ddd; border: 1px solid #444; "
        "border-radius: 3px; padding: 2px 6px; font-size: 11px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #2d2d38; color: #ddd; "
        "border: 1px solid #444; selection-background-color: #4682dc; }"
    );
    connect(filter_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        setFilterLevel(static_cast<ConsoleLevel>(idx));
    });
    toolbar_layout->addWidget(filter_combo_);

    toolbar_layout->addStretch();

    // 초기화 버튼
    clear_button_ = new QPushButton("🗑 초기화", toolbar);
    clear_button_->setStyleSheet(
        "QPushButton { color: #888; background: transparent; border: none; font-size: 11px; }"
        "QPushButton:hover { color: #ddd; }");
    connect(clear_button_, &QPushButton::clicked, this, &ConsoleTab::clear);
    toolbar_layout->addWidget(clear_button_);

    layout->addWidget(toolbar);

    // 출력 영역
    output_ = new QPlainTextEdit(this);
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(MAX_MESSAGES);
    output_->setStyleSheet(
        "QPlainTextEdit {"
        "  background: #1a1a22;"
        "  color: #ddd;"
        "  border: none;"
        "  font-family: 'SF Mono', 'Menlo', 'Monaco', monospace;"
        "  font-size: 12px;"
        "  padding: 8px;"
        "}"
    );
    layout->addWidget(output_, 1);

    // 입력 영역
    auto* input_container = new QWidget(this);
    input_container->setFixedHeight(32);
    input_container->setStyleSheet(
        "QWidget { background: #22222c; border-top: 1px solid #333; }");
    auto* input_layout = new QHBoxLayout(input_container);
    input_layout->setContentsMargins(8, 2, 8, 2);
    input_layout->setSpacing(4);

    // 프롬프트 라벨
    auto* prompt = new QLabel("❯", input_container);
    prompt->setStyleSheet("QLabel { color: #4682dc; font-size: 14px; font-weight: bold; }");
    input_layout->addWidget(prompt);

    // 입력 필드
    input_ = new QLineEdit(input_container);
    input_->setPlaceholderText("JavaScript 코드 입력...");
    input_->setStyleSheet(
        "QLineEdit {"
        "  background: transparent;"
        "  color: #e0e0e5;"
        "  border: none;"
        "  font-family: 'SF Mono', 'Menlo', 'Monaco', monospace;"
        "  font-size: 12px;"
        "}"
    );
    connect(input_, &QLineEdit::returnPressed, this, &ConsoleTab::onInputSubmit);

    // 키보드 이벤트로 히스토리 탐색
    input_->installEventFilter(this);

    input_layout->addWidget(input_, 1);
    layout->addWidget(input_container);

    // 초기 메시지
    ConsoleMessage welcome;
    welcome.level = ConsoleLevel::Info;
    welcome.text = "Ordinal Browser 개발자 콘솔 v0.1.0";
    welcome.timestamp = QDateTime::currentDateTime();
    addMessage(welcome);
}

void ConsoleTab::addMessage(const ConsoleMessage& msg) {
    messages_.push_back(msg);
    if (static_cast<int>(messages_.size()) > MAX_MESSAGES) {
        messages_.pop_front();
    }

    // 필터 확인
    if (msg.level < filter_level_) return;

    appendFormattedMessage(msg);
}

void ConsoleTab::clear() {
    messages_.clear();
    output_->clear();
}

void ConsoleTab::setFilterLevel(ConsoleLevel min_level) {
    filter_level_ = min_level;

    // 필터 변경 시 출력 재구성
    output_->clear();
    for (const auto& msg : messages_) {
        if (msg.level >= filter_level_) {
            appendFormattedMessage(msg);
        }
    }
}

void ConsoleTab::onInputSubmit() {
    QString code = input_->text().trimmed();
    if (code.isEmpty()) return;

    // 명령어 히스토리에 추가
    command_history_.push_back(code);
    if (static_cast<int>(command_history_.size()) > MAX_HISTORY) {
        command_history_.erase(command_history_.begin());
    }
    history_index_ = -1;

    // 입력 표시
    ConsoleMessage input_msg;
    input_msg.level = ConsoleLevel::Log;
    input_msg.text = "❯ " + code;
    input_msg.timestamp = QDateTime::currentDateTime();
    appendFormattedMessage(input_msg);

    // 실행 요청
    emit executeRequested(code);

    // 내장 명령 처리
    if (code == "clear" || code == "cls") {
        clear();
    } else if (code == "help") {
        ConsoleMessage help;
        help.level = ConsoleLevel::Info;
        help.text = "사용 가능한 명령:\n"
                    "  clear / cls  — 콘솔 초기화\n"
                    "  help         — 도움말 표시\n"
                    "  history      — 명령어 히스토리\n"
                    "  어떤 JavaScript 코드든 입력하여 실행";
        help.timestamp = QDateTime::currentDateTime();
        addMessage(help);
    } else if (code == "history") {
        QString hist_text = "명령어 히스토리:\n";
        int start = std::max(0, static_cast<int>(command_history_.size()) - 20);
        for (int i = start; i < static_cast<int>(command_history_.size()); ++i) {
            hist_text += QString("  %1: %2\n").arg(i + 1).arg(command_history_[i]);
        }
        ConsoleMessage hist;
        hist.level = ConsoleLevel::Info;
        hist.text = hist_text;
        hist.timestamp = QDateTime::currentDateTime();
        addMessage(hist);
    }

    input_->clear();
}

void ConsoleTab::appendFormattedMessage(const ConsoleMessage& msg) {
    QString prefix = levelPrefix(msg.level);
    QColor color = levelColor(msg.level);

    // 타임스탬프
    QString time_str = msg.timestamp.toString("HH:mm:ss.zzz");

    // 서식 있는 텍스트 추가
    QString html = QString("<span style='color: #555;'>[%1]</span> "
                          "<span style='color: %2;'>%3</span> "
                          "<span style='color: %4;'>%5</span>")
        .arg(time_str, color.name(), prefix, color.name(),
             msg.text.toHtmlEscaped().replace("\n", "<br>"));

    if (!msg.source.isEmpty()) {
        html += QString(" <span style='color: #555;'>(%1)</span>").arg(msg.source);
    }

    output_->appendHtml(html);

    // 자동 스크롤
    auto* scrollbar = output_->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

QString ConsoleTab::levelPrefix(ConsoleLevel level) const {
    switch (level) {
        case ConsoleLevel::Log:    return "[LOG]";
        case ConsoleLevel::Info:   return "[INFO]";
        case ConsoleLevel::Warn:   return "[WARN]";
        case ConsoleLevel::Error:  return "[ERROR]";
        case ConsoleLevel::Debug:  return "[DEBUG]";
        case ConsoleLevel::Result: return "[→]";
    }
    return "[?]";
}

QColor ConsoleTab::levelColor(ConsoleLevel level) const {
    switch (level) {
        case ConsoleLevel::Log:    return QColor(200, 200, 210);
        case ConsoleLevel::Info:   return QColor(100, 149, 237);
        case ConsoleLevel::Warn:   return QColor(255, 193, 7);
        case ConsoleLevel::Error:  return QColor(244, 67, 54);
        case ConsoleLevel::Debug:  return QColor(150, 150, 170);
        case ConsoleLevel::Result: return QColor(76, 175, 80);
    }
    return QColor(200, 200, 210);
}

// ============================================================
// NetworkTab 구현
// ============================================================

NetworkTab::NetworkTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 상단 툴바
    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(30);
    toolbar->setStyleSheet("QWidget { background: #252530; border-bottom: 1px solid #333; }");
    auto* toolbar_layout = new QHBoxLayout(toolbar);
    toolbar_layout->setContentsMargins(8, 2, 8, 2);
    toolbar_layout->setSpacing(8);

    // URL 필터
    filter_input_ = new QLineEdit(toolbar);
    filter_input_->setPlaceholderText("요청 필터...");
    filter_input_->setStyleSheet(
        "QLineEdit { background: #2d2d38; color: #ddd; border: 1px solid #444; "
        "border-radius: 3px; padding: 2px 8px; font-size: 11px; }"
    );
    filter_input_->setMaximumWidth(200);
    toolbar_layout->addWidget(filter_input_);

    // 유형 필터
    type_filter_ = new QComboBox(toolbar);
    type_filter_->addItems({"전체", "문서", "스크립트", "스타일", "이미지", "XHR", "기타"});
    type_filter_->setStyleSheet(
        "QComboBox { background: #2d2d38; color: #ddd; border: 1px solid #444; "
        "border-radius: 3px; padding: 2px 6px; font-size: 11px; }"
    );
    toolbar_layout->addWidget(type_filter_);

    toolbar_layout->addStretch();

    // 요약 라벨
    summary_label_ = new QLabel("요청 0건 | 0 B | 0 ms", toolbar);
    summary_label_->setStyleSheet("QLabel { color: #888; font-size: 11px; }");
    toolbar_layout->addWidget(summary_label_);

    // 초기화 버튼
    clear_button_ = new QPushButton("🗑", toolbar);
    clear_button_->setStyleSheet(
        "QPushButton { color: #888; background: transparent; border: none; font-size: 12px; }"
        "QPushButton:hover { color: #ddd; }");
    connect(clear_button_, &QPushButton::clicked, this, &NetworkTab::clear);
    toolbar_layout->addWidget(clear_button_);

    layout->addWidget(toolbar);

    // 요청 테이블
    table_model_ = new QStandardItemModel(0, 7, this);
    table_model_->setHorizontalHeaderLabels({
        "메서드", "URL", "상태", "유형", "크기", "시간", "워터폴"
    });

    table_view_ = new QTableView(this);
    table_view_->setModel(table_model_);
    table_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_view_->setAlternatingRowColors(true);
    table_view_->setShowGrid(false);
    table_view_->verticalHeader()->setVisible(false);
    table_view_->setSortingEnabled(true);

    // 열 너비 설정
    auto* header = table_view_->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::Fixed);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::Fixed);
    header->setSectionResizeMode(3, QHeaderView::Fixed);
    header->setSectionResizeMode(4, QHeaderView::Fixed);
    header->setSectionResizeMode(5, QHeaderView::Fixed);
    header->setSectionResizeMode(6, QHeaderView::Fixed);
    table_view_->setColumnWidth(0, 60);
    table_view_->setColumnWidth(2, 50);
    table_view_->setColumnWidth(3, 80);
    table_view_->setColumnWidth(4, 70);
    table_view_->setColumnWidth(5, 70);
    table_view_->setColumnWidth(6, 150);

    table_view_->setStyleSheet(
        "QTableView {"
        "  background: #1a1a22;"
        "  color: #ddd;"
        "  border: none;"
        "  font-family: 'SF Mono', monospace;"
        "  font-size: 11px;"
        "}"
        "QTableView::item { padding: 3px 6px; }"
        "QTableView::item:selected { background: #3a3a50; }"
        "QTableView::item:alternate { background: #1e1e28; }"
        "QHeaderView::section {"
        "  background: #252530;"
        "  color: #999;"
        "  font-size: 10px;"
        "  font-weight: bold;"
        "  padding: 4px;"
        "  border: none;"
        "  border-bottom: 1px solid #444;"
        "  border-right: 1px solid #333;"
        "}"
    );

    // 행 선택 시그널
    connect(table_view_->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex& current, const QModelIndex&) {
        if (current.isValid()) {
            emit requestSelected(current.row());
        }
    });

    layout->addWidget(table_view_, 1);
}

void NetworkTab::addRequest(const NetworkRequest& request) {
    NetworkRequest req = request;
    req.id = next_id_++;
    requests_.push_back(req);

    int row = table_model_->rowCount();
    table_model_->insertRow(row);

    // 메서드
    auto* method_item = new QStandardItem(req.method);
    QColor method_color;
    if (req.method == "GET") method_color = QColor(100, 149, 237);
    else if (req.method == "POST") method_color = QColor(76, 175, 80);
    else if (req.method == "PUT") method_color = QColor(255, 193, 7);
    else if (req.method == "DELETE") method_color = QColor(244, 67, 54);
    else method_color = QColor(200, 200, 210);
    method_item->setForeground(method_color);
    table_model_->setItem(row, 0, method_item);

    // URL (경로만 표시)
    QUrl parsed_url(req.url);
    QString display_path = parsed_url.path();
    if (display_path.isEmpty()) display_path = "/";
    auto* url_item = new QStandardItem(display_path);
    url_item->setToolTip(req.url);
    table_model_->setItem(row, 1, url_item);

    // 상태 코드
    auto* status_item = new QStandardItem(QString::number(req.status_code));
    if (req.status_code >= 200 && req.status_code < 300) {
        status_item->setForeground(QColor(76, 175, 80));
    } else if (req.status_code >= 300 && req.status_code < 400) {
        status_item->setForeground(QColor(100, 149, 237));
    } else if (req.status_code >= 400) {
        status_item->setForeground(QColor(244, 67, 54));
    }
    if (req.blocked) {
        status_item->setText("차단");
        status_item->setForeground(QColor(244, 67, 54));
    }
    table_model_->setItem(row, 2, status_item);

    // 콘텐츠 유형 (간략화)
    QString type_short = req.content_type;
    if (type_short.contains('/')) {
        type_short = type_short.split('/').last().split(';').first();
    }
    table_model_->setItem(row, 3, new QStandardItem(type_short));

    // 크기
    table_model_->setItem(row, 4, new QStandardItem(formatSize(req.size_bytes)));

    // 시간
    auto* time_item = new QStandardItem(formatTime(req.time_ms));
    if (req.time_ms > 1000) {
        time_item->setForeground(QColor(244, 67, 54));
    } else if (req.time_ms > 500) {
        time_item->setForeground(QColor(255, 193, 7));
    }
    table_model_->setItem(row, 5, time_item);

    // 워터폴 (텍스트 기반 바)
    int total_width = 30;
    int dns_w = static_cast<int>(total_width * req.dns_ms / std::max(req.time_ms, 1.0));
    int conn_w = static_cast<int>(total_width * req.connect_ms / std::max(req.time_ms, 1.0));
    int ssl_w = static_cast<int>(total_width * req.ssl_ms / std::max(req.time_ms, 1.0));
    int ttfb_w = static_cast<int>(total_width * req.ttfb_ms / std::max(req.time_ms, 1.0));
    int dl_w = total_width - dns_w - conn_w - ssl_w - ttfb_w;
    if (dl_w < 0) dl_w = 0;

    QString waterfall;
    waterfall += QString("█").repeated(dns_w);   // DNS (파란색)
    waterfall += QString("▓").repeated(conn_w);  // 연결 (주황색)
    waterfall += QString("▒").repeated(ssl_w);   // SSL (보라색)
    waterfall += QString("░").repeated(ttfb_w);  // TTFB (녹색)
    waterfall += QString("▪").repeated(dl_w);    // 다운로드 (회색)

    auto* waterfall_item = new QStandardItem(waterfall);
    waterfall_item->setToolTip(
        QString("DNS: %1ms | 연결: %2ms | SSL: %3ms | TTFB: %4ms | 다운로드: %5ms")
            .arg(req.dns_ms, 0, 'f', 1)
            .arg(req.connect_ms, 0, 'f', 1)
            .arg(req.ssl_ms, 0, 'f', 1)
            .arg(req.ttfb_ms, 0, 'f', 1)
            .arg(req.download_ms, 0, 'f', 1));
    table_model_->setItem(row, 6, waterfall_item);

    updateSummary();
}

void NetworkTab::clear() {
    requests_.clear();
    table_model_->removeRows(0, table_model_->rowCount());
    next_id_ = 1;
    updateSummary();
}

void NetworkTab::setFilter(const QString& filter) {
    if (filter_input_) {
        filter_input_->setText(filter);
    }
}

void NetworkTab::updateSummary() {
    qint64 total_size = 0;
    double total_time = 0;
    for (const auto& req : requests_) {
        total_size += req.size_bytes;
        total_time = std::max(total_time, req.time_ms);
    }

    if (summary_label_) {
        summary_label_->setText(
            QString("요청 %1건 | %2 | %3")
                .arg(requests_.size())
                .arg(formatSize(total_size))
                .arg(formatTime(total_time)));
    }
}

QString NetworkTab::formatSize(qint64 bytes) const {
    if (bytes < 1024) return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
}

QString NetworkTab::formatTime(double ms) const {
    if (ms < 1.0) return "<1ms";
    if (ms < 1000.0) return QString::number(ms, 'f', 0) + " ms";
    return QString::number(ms / 1000.0, 'f', 2) + " s";
}

// ============================================================
// SecurityTab 구현
// ============================================================

SecurityTab::SecurityTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 상단 툴바
    auto* toolbar = new QWidget(this);
    toolbar->setFixedHeight(30);
    toolbar->setStyleSheet("QWidget { background: #252530; border-bottom: 1px solid #333; }");
    auto* toolbar_layout = new QHBoxLayout(toolbar);
    toolbar_layout->setContentsMargins(8, 2, 8, 2);
    toolbar_layout->setSpacing(8);

    // 스캔 버튼
    scan_button_ = new QPushButton("🔍 보안 스캔 실행", toolbar);
    scan_button_->setCursor(Qt::PointingHandCursor);
    scan_button_->setStyleSheet(
        "QPushButton { color: #ddd; background: #3a3a50; border: 1px solid #4682dc; "
        "border-radius: 3px; padding: 3px 12px; font-size: 11px; }"
        "QPushButton:hover { background: #4682dc; }");
    connect(scan_button_, &QPushButton::clicked, this, [this]() {
        emit scanRequested();
    });
    toolbar_layout->addWidget(scan_button_);

    // 스캔 진행 바
    scan_progress_ = new QProgressBar(toolbar);
    scan_progress_->setRange(0, 100);
    scan_progress_->setValue(0);
    scan_progress_->setFixedHeight(16);
    scan_progress_->setMaximumWidth(150);
    scan_progress_->setTextVisible(true);
    scan_progress_->setVisible(false);
    scan_progress_->setStyleSheet(
        "QProgressBar { background: #2d2d38; border: 1px solid #444; border-radius: 3px; "
        "font-size: 9px; color: #ddd; text-align: center; }"
        "QProgressBar::chunk { background: #4682dc; border-radius: 2px; }"
    );
    toolbar_layout->addWidget(scan_progress_);

    toolbar_layout->addStretch();

    // 상태 라벨
    status_label_ = new QLabel("스캔 대기 중", toolbar);
    status_label_->setStyleSheet("QLabel { color: #888; font-size: 11px; }");
    toolbar_layout->addWidget(status_label_);

    // 요약 라벨
    summary_label_ = new QLabel("", toolbar);
    summary_label_->setStyleSheet("QLabel { color: #888; font-size: 11px; }");
    toolbar_layout->addWidget(summary_label_);

    layout->addWidget(toolbar);

    // 발견 사항 트리
    findings_tree_ = new QTreeWidget(this);
    findings_tree_->setHeaderLabels({"분류", "심각도", "설명", "권장 조치"});
    findings_tree_->setAlternatingRowColors(true);
    findings_tree_->setRootIsDecorated(true);
    findings_tree_->setSortingEnabled(true);

    auto* tree_header = findings_tree_->header();
    tree_header->setSectionResizeMode(0, QHeaderView::Fixed);
    tree_header->setSectionResizeMode(1, QHeaderView::Fixed);
    tree_header->setSectionResizeMode(2, QHeaderView::Stretch);
    tree_header->setSectionResizeMode(3, QHeaderView::Stretch);
    findings_tree_->setColumnWidth(0, 120);
    findings_tree_->setColumnWidth(1, 70);

    findings_tree_->setStyleSheet(
        "QTreeWidget {"
        "  background: #1a1a22;"
        "  color: #ddd;"
        "  border: none;"
        "  font-size: 11px;"
        "}"
        "QTreeWidget::item { padding: 3px; }"
        "QTreeWidget::item:selected { background: #3a3a50; }"
        "QTreeWidget::item:alternate { background: #1e1e28; }"
        "QHeaderView::section {"
        "  background: #252530;"
        "  color: #999;"
        "  font-size: 10px;"
        "  font-weight: bold;"
        "  padding: 4px;"
        "  border: none;"
        "  border-bottom: 1px solid #444;"
        "}"
    );

    connect(findings_tree_, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (current) {
            int idx = findings_tree_->indexOfTopLevelItem(current);
            if (idx >= 0) emit findingSelected(idx);
        }
    });

    layout->addWidget(findings_tree_, 1);
}

void SecurityTab::addFinding(const SecurityFinding& finding) {
    findings_.push_back(finding);

    auto* item = new QTreeWidgetItem(findings_tree_);
    item->setText(0, finding.category);

    // 심각도별 색상
    item->setText(1, finding.severity);
    QColor severity_color;
    if (finding.severity == "Critical") severity_color = QColor(244, 67, 54);
    else if (finding.severity == "High") severity_color = QColor(255, 152, 0);
    else if (finding.severity == "Medium") severity_color = QColor(255, 193, 7);
    else if (finding.severity == "Low") severity_color = QColor(100, 149, 237);
    else severity_color = QColor(150, 150, 160);
    item->setForeground(1, severity_color);

    item->setText(2, finding.description);
    item->setText(3, finding.recommendation);

    // 관련 요소가 있으면 하위 항목으로 추가
    if (!finding.element.isEmpty()) {
        auto* child = new QTreeWidgetItem(item);
        child->setText(0, "요소");
        child->setText(2, finding.element);
        child->setForeground(0, QColor(150, 150, 170));
        child->setForeground(2, QColor(150, 150, 170));
    }

    // 요약 업데이트
    int critical = 0, high = 0, medium = 0, low = 0;
    for (const auto& f : findings_) {
        if (f.severity == "Critical") critical++;
        else if (f.severity == "High") high++;
        else if (f.severity == "Medium") medium++;
        else if (f.severity == "Low") low++;
    }
    if (summary_label_) {
        summary_label_->setText(
            QString("심각 %1 | 높음 %2 | 보통 %3 | 낮음 %4")
                .arg(critical).arg(high).arg(medium).arg(low));
    }
}

void SecurityTab::clear() {
    findings_.clear();
    findings_tree_->clear();
    if (summary_label_) summary_label_->clear();
    if (status_label_) status_label_->setText("스캔 대기 중");
}

void SecurityTab::setScanProgress(int percent) {
    if (scan_progress_) {
        scan_progress_->setVisible(percent > 0 && percent < 100);
        scan_progress_->setValue(percent);
    }
    if (status_label_) {
        if (percent <= 0) {
            status_label_->setText("스캔 대기 중");
        } else if (percent >= 100) {
            status_label_->setText(
                QString("스캔 완료 — %1건 발견").arg(findings_.size()));
        } else {
            status_label_->setText(
                QString("스캔 중... %1%").arg(percent));
        }
    }
}

// ============================================================
// ElementsTab 구현
// ============================================================

ElementsTab::ElementsTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 상단 검색 바
    auto* search_bar = new QWidget(this);
    search_bar->setFixedHeight(30);
    search_bar->setStyleSheet("QWidget { background: #252530; border-bottom: 1px solid #333; }");
    auto* search_layout = new QHBoxLayout(search_bar);
    search_layout->setContentsMargins(8, 2, 8, 2);

    search_input_ = new QLineEdit(search_bar);
    search_input_->setPlaceholderText("CSS 셀렉터로 검색... (예: div.class, #id)");
    search_input_->setStyleSheet(
        "QLineEdit { background: #2d2d38; color: #ddd; border: 1px solid #444; "
        "border-radius: 3px; padding: 2px 8px; font-size: 11px; "
        "font-family: 'SF Mono', monospace; }");
    search_layout->addWidget(search_input_);

    layout->addWidget(search_bar);

    // 스플리터 (DOM 트리 + 소스 뷰)
    splitter_ = new QSplitter(Qt::Horizontal, this);

    // DOM 트리
    dom_tree_ = new QTreeWidget(splitter_);
    dom_tree_->setHeaderLabels({"요소", "속성"});
    dom_tree_->setAlternatingRowColors(true);
    dom_tree_->setAnimated(true);
    dom_tree_->setStyleSheet(
        "QTreeWidget {"
        "  background: #1a1a22;"
        "  color: #ddd;"
        "  border: none;"
        "  font-family: 'SF Mono', 'Menlo', monospace;"
        "  font-size: 12px;"
        "}"
        "QTreeWidget::item { padding: 2px; }"
        "QTreeWidget::item:selected { background: #3a3a50; }"
        "QTreeWidget::item:alternate { background: #1e1e28; }"
        "QHeaderView::section {"
        "  background: #252530;"
        "  color: #999;"
        "  font-size: 10px;"
        "  font-weight: bold;"
        "  padding: 4px;"
        "  border: none;"
        "  border-bottom: 1px solid #444;"
        "}"
    );

    auto* tree_header = dom_tree_->header();
    tree_header->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_header->setSectionResizeMode(1, QHeaderView::Stretch);

    connect(dom_tree_, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (current) {
            emit nodeSelected(current->data(0, Qt::UserRole).toString());
        }
    });

    splitter_->addWidget(dom_tree_);

    // 소스 코드 뷰
    source_view_ = new QPlainTextEdit(splitter_);
    source_view_->setReadOnly(true);
    source_view_->setStyleSheet(
        "QPlainTextEdit {"
        "  background: #1a1a22;"
        "  color: #b0b0b5;"
        "  border: none;"
        "  border-left: 1px solid #333;"
        "  font-family: 'SF Mono', 'Menlo', monospace;"
        "  font-size: 11px;"
        "  padding: 8px;"
        "}"
    );

    splitter_->addWidget(source_view_);
    splitter_->setStretchFactor(0, 2);
    splitter_->setStretchFactor(1, 1);

    layout->addWidget(splitter_, 1);
}

void ElementsTab::setDomTree(const QString& html_source) {
    dom_tree_->clear();
    source_view_->setPlainText(html_source);
    parseHtmlToTree(html_source);
}

void ElementsTab::addNode(const QString& tag, const QString& attributes,
                           const QString& parent_path) {
    QTreeWidgetItem* parent_item = nullptr;
    if (!parent_path.isEmpty()) {
        parent_item = findNodeByPath(parent_path);
    }

    QTreeWidgetItem* item;
    if (parent_item) {
        item = new QTreeWidgetItem(parent_item);
    } else {
        item = new QTreeWidgetItem(dom_tree_);
    }

    // 태그명에 색상 적용
    item->setText(0, "<" + tag + ">");
    item->setText(1, attributes);

    // 태그별 색상
    QColor tag_color;
    if (tag == "html" || tag == "head" || tag == "body") {
        tag_color = QColor(86, 156, 214);   // 파란색 — 구조 태그
    } else if (tag == "div" || tag == "span" || tag == "p" || tag == "section") {
        tag_color = QColor(78, 201, 176);   // 청록색 — 콘텐츠 태그
    } else if (tag == "script" || tag == "style" || tag == "link") {
        tag_color = QColor(206, 145, 120);  // 주황색 — 리소스 태그
    } else if (tag == "a" || tag == "button" || tag == "input" || tag == "form") {
        tag_color = QColor(220, 220, 170);  // 노란색 — 대화형 태그
    } else {
        tag_color = QColor(200, 200, 210);
    }
    item->setForeground(0, tag_color);
    item->setForeground(1, QColor(150, 150, 170));

    // 경로 데이터 저장
    QString path = parent_path.isEmpty() ? tag : parent_path + "/" + tag;
    item->setData(0, Qt::UserRole, path);
}

void ElementsTab::clear() {
    dom_tree_->clear();
    source_view_->clear();
}

void ElementsTab::parseHtmlToTree(const QString& html) {
    // 간단한 HTML 태그 파싱하여 트리 구성
    static QRegularExpression tag_regex(
        R"(<\s*(\/?)([a-zA-Z][a-zA-Z0-9]*)\s*([^>]*?)(\/?)>)");

    std::vector<QTreeWidgetItem*> stack;
    auto it = tag_regex.globalMatch(html);

    while (it.hasNext()) {
        auto match = it.next();
        bool is_closing = !match.captured(1).isEmpty();
        QString tag_name = match.captured(2).toLower();
        QString attrs = match.captured(3).trimmed();
        bool is_self_closing = !match.captured(4).isEmpty();

        // 닫는 태그 처리
        if (is_closing) {
            if (!stack.empty()) {
                stack.pop_back();
            }
            continue;
        }

        // 새 노드 생성
        QTreeWidgetItem* item;
        if (stack.empty()) {
            item = new QTreeWidgetItem(dom_tree_);
        } else {
            item = new QTreeWidgetItem(stack.back());
        }

        item->setText(0, "<" + tag_name + ">");
        item->setText(1, attrs);

        // 태그별 색상
        QColor tag_color;
        if (tag_name == "html" || tag_name == "head" || tag_name == "body") {
            tag_color = QColor(86, 156, 214);
        } else if (tag_name == "div" || tag_name == "span" || tag_name == "p") {
            tag_color = QColor(78, 201, 176);
        } else if (tag_name == "script" || tag_name == "style") {
            tag_color = QColor(206, 145, 120);
        } else if (tag_name == "a" || tag_name == "button" || tag_name == "input") {
            tag_color = QColor(220, 220, 170);
        } else {
            tag_color = QColor(200, 200, 210);
        }
        item->setForeground(0, tag_color);
        item->setForeground(1, QColor(150, 150, 170));

        // 셀프 클로징 태그가 아니면 스택에 추가
        static const QStringList void_elements = {
            "area", "base", "br", "col", "embed", "hr", "img", "input",
            "link", "meta", "param", "source", "track", "wbr"
        };

        if (!is_self_closing && !void_elements.contains(tag_name)) {
            stack.push_back(item);
        }
    }

    // 모든 최상위 항목 펼치기
    dom_tree_->expandToDepth(2);
}

QTreeWidgetItem* ElementsTab::findNodeByPath(const QString& path) {
    QStringList parts = path.split('/');
    QTreeWidgetItem* current = nullptr;

    for (int i = 0; i < dom_tree_->topLevelItemCount(); ++i) {
        auto* item = dom_tree_->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString().startsWith(parts.first())) {
            current = item;
            break;
        }
    }

    if (!current || parts.size() <= 1) return current;

    for (int p = 1; p < parts.size() && current; ++p) {
        bool found = false;
        for (int c = 0; c < current->childCount(); ++c) {
            auto* child = current->child(c);
            QString child_path = child->data(0, Qt::UserRole).toString();
            if (child_path.endsWith(parts[p])) {
                current = child;
                found = true;
                break;
            }
        }
        if (!found) return nullptr;
    }

    return current;
}

// ============================================================
// DevToolsPanel 구현
// ============================================================

DevToolsPanel::DevToolsPanel(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(200);
    setMaximumHeight(500);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 상단 바 (제목 + 닫기)
    auto* title_bar = new QWidget(this);
    title_bar->setFixedHeight(28);
    title_bar->setStyleSheet(
        "QWidget { background: #252530; border-top: 2px solid #4682dc; }");
    auto* title_layout = new QHBoxLayout(title_bar);
    title_layout->setContentsMargins(12, 0, 8, 0);

    auto* title = new QLabel("🔧 개발자 도구", title_bar);
    title->setStyleSheet("QLabel { color: #ddd; font-size: 12px; font-weight: bold; }");
    title_layout->addWidget(title);
    title_layout->addStretch();

    close_button_ = new QPushButton("✕", title_bar);
    close_button_->setFixedSize(20, 20);
    close_button_->setCursor(Qt::PointingHandCursor);
    close_button_->setStyleSheet(
        "QPushButton { color: #888; background: transparent; border: none; font-size: 12px; border-radius: 10px; }"
        "QPushButton:hover { color: #fff; background: #e74c3c; }");
    connect(close_button_, &QPushButton::clicked, this, [this]() {
        setVisible(false);
        emit panelClosed();
    });
    title_layout->addWidget(close_button_);

    layout->addWidget(title_bar);

    // 탭 위젯
    tab_widget_ = new QTabWidget(this);
    tab_widget_->setStyleSheet(
        "QTabWidget::pane { border: none; background: #1a1a22; }"
        "QTabBar {"
        "  background: #22222c;"
        "  border-bottom: 1px solid #333;"
        "}"
        "QTabBar::tab {"
        "  background: #22222c;"
        "  color: #888;"
        "  padding: 6px 16px;"
        "  border: none;"
        "  border-bottom: 2px solid transparent;"
        "  font-size: 11px;"
        "}"
        "QTabBar::tab:selected {"
        "  color: #ddd;"
        "  border-bottom-color: #4682dc;"
        "  font-weight: bold;"
        "}"
        "QTabBar::tab:hover {"
        "  color: #bbb;"
        "  background: #2a2a35;"
        "}"
    );

    // 콘솔 탭
    console_tab_ = new ConsoleTab(this);
    tab_widget_->addTab(console_tab_, "🖥 콘솔");

    // 네트워크 탭
    network_tab_ = new NetworkTab(this);
    tab_widget_->addTab(network_tab_, "🌐 네트워크");

    // 보안 탭
    security_tab_ = new SecurityTab(this);
    tab_widget_->addTab(security_tab_, "🛡 보안");

    // 요소 탭
    elements_tab_ = new ElementsTab(this);
    tab_widget_->addTab(elements_tab_, "📄 요소");

    layout->addWidget(tab_widget_, 1);
}

void DevToolsPanel::showTab(int index) {
    if (tab_widget_ && index >= 0 && index < tab_widget_->count()) {
        tab_widget_->setCurrentIndex(index);
        setVisible(true);
    }
}

} // namespace ordinal::ui
