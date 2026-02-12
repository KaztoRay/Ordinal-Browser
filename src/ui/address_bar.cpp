/**
 * @file address_bar.cpp
 * @brief 주소 바 위젯 구현
 * 
 * URL 입력 필드에 자동완성 팝업, 보안 잠금 아이콘,
 * 검색 엔진 통합 (비-URL 입력 감지), 진행 바 오버레이를
 * 포함한 전체 구현입니다.
 */

#include "address_bar.h"

#include <QHBoxLayout>
#include <QRegularExpression>
#include <QDateTime>
#include <QPainter>
#include <QStyle>
#include <QToolTip>
#include <algorithm>

namespace ordinal::ui {

// ============================================================
// AutocompleteModel 구현
// ============================================================

AutocompleteModel::AutocompleteModel(QObject* parent)
    : QStringListModel(parent) {
    history_.reserve(MAX_HISTORY);
}

void AutocompleteModel::addHistoryEntry(const QString& url, const QString& title) {
    // 기존 항목 확인 — 이미 있으면 방문 횟수 증가
    for (auto& entry : history_) {
        if (entry.url == url) {
            entry.visit_count++;
            entry.last_visit = QDateTime::currentMSecsSinceEpoch();
            if (!title.isEmpty()) {
                entry.title = title;
            }
            return;
        }
    }

    // 히스토리 최대 크기 초과 시 가장 오래된 항목 제거
    if (static_cast<int>(history_.size()) >= MAX_HISTORY) {
        // 방문 횟수가 가장 적고 오래된 항목 제거
        auto it = std::min_element(history_.begin(), history_.end(),
            [](const HistoryEntry& a, const HistoryEntry& b) {
                if (a.visit_count != b.visit_count) return a.visit_count < b.visit_count;
                return a.last_visit < b.last_visit;
            });
        if (it != history_.end()) {
            history_.erase(it);
        }
    }

    // 새 항목 추가
    history_.push_back({
        url,
        title,
        1,
        QDateTime::currentMSecsSinceEpoch()
    });
}

void AutocompleteModel::updateSuggestions(const QString& input) {
    if (input.isEmpty()) {
        setStringList({});
        return;
    }

    QString lower_input = input.toLower();

    // 관련성 점수 기반 정렬
    struct ScoredEntry {
        QString display;
        double score{0.0};
    };

    std::vector<ScoredEntry> scored;
    scored.reserve(history_.size());

    for (const auto& entry : history_) {
        QString lower_url = entry.url.toLower();
        QString lower_title = entry.title.toLower();

        double score = 0.0;

        // URL 매칭 점수
        if (lower_url.startsWith(lower_input)) {
            score += 100.0;
        } else if (lower_url.contains(lower_input)) {
            score += 50.0;
        }

        // 제목 매칭 점수
        if (lower_title.startsWith(lower_input)) {
            score += 80.0;
        } else if (lower_title.contains(lower_input)) {
            score += 30.0;
        }

        // 방문 횟수 보너스
        score += entry.visit_count * 5.0;

        // 최근성 보너스 (7일 이내)
        qint64 age_ms = QDateTime::currentMSecsSinceEpoch() - entry.last_visit;
        double age_days = static_cast<double>(age_ms) / (1000.0 * 60 * 60 * 24);
        if (age_days < 7.0) {
            score += (7.0 - age_days) * 3.0;
        }

        if (score > 0.0) {
            QString display = entry.title.isEmpty()
                ? entry.url
                : entry.title + " — " + entry.url;
            scored.push_back({display, score});
        }
    }

    // 점수 기준 내림차순 정렬
    std::sort(scored.begin(), scored.end(),
        [](const ScoredEntry& a, const ScoredEntry& b) {
            return a.score > b.score;
        });

    // 최대 제안 수만큼 추출
    QStringList suggestions;
    int count = std::min(MAX_SUGGESTIONS, static_cast<int>(scored.size()));
    for (int i = 0; i < count; ++i) {
        suggestions.append(scored[i].display);
    }

    setStringList(suggestions);
}

void AutocompleteModel::clearHistory() {
    history_.clear();
    setStringList({});
}

// ============================================================
// AddressBar 구현
// ============================================================

AddressBar::AddressBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(34);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(0);

    // 보안 아이콘 (왼쪽)
    security_icon_ = new QLabel("🟢", this);
    security_icon_->setFixedSize(28, 28);
    security_icon_->setAlignment(Qt::AlignCenter);
    security_icon_->setCursor(Qt::PointingHandCursor);
    security_icon_->setStyleSheet(
        "QLabel { font-size: 14px; padding: 2px; border-radius: 4px; }"
        "QLabel:hover { background: #3a3a45; }"
    );
    security_icon_->installEventFilter(this);
    layout->addWidget(security_icon_);

    // URL 입력 필드
    url_input_ = new QLineEdit(this);
    url_input_->setPlaceholderText("URL 입력 또는 검색...");
    url_input_->setStyleSheet(
        "QLineEdit {"
        "  background: #25252d;"
        "  color: #e0e0e5;"
        "  border: 1px solid #3a3a45;"
        "  border-radius: 6px;"
        "  padding: 4px 12px;"
        "  font-size: 13px;"
        "  selection-background-color: #4682dc;"
        "}"
        "QLineEdit:focus {"
        "  border-color: #4682dc;"
        "  background: #2a2a35;"
        "}"
    );

    // 자동완성 설정
    autocomplete_model_ = new AutocompleteModel(this);
    completer_ = new QCompleter(autocomplete_model_, this);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setMaxVisibleItems(8);
    completer_->setFilterMode(Qt::MatchContains);

    // 자동완성 팝업 스타일
    auto* popup = completer_->popup();
    popup->setStyleSheet(
        "QListView {"
        "  background: #2d2d38;"
        "  color: #ddd;"
        "  border: 1px solid #444;"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "  font-size: 12px;"
        "}"
        "QListView::item {"
        "  padding: 6px 8px;"
        "  border-radius: 3px;"
        "}"
        "QListView::item:selected {"
        "  background: #4682dc;"
        "  color: #fff;"
        "}"
        "QListView::item:hover {"
        "  background: #3a3a48;"
        "}"
    );

    url_input_->setCompleter(completer_);
    layout->addWidget(url_input_, 1);

    // 시그널 연결
    connect(url_input_, &QLineEdit::returnPressed, this, &AddressBar::onReturnPressed);
    connect(url_input_, &QLineEdit::textChanged, this, &AddressBar::onTextChanged);

    // 자동완성 항목 선택 시
    connect(completer_, QOverload<const QString&>::of(&QCompleter::activated),
            this, [this](const QString& text) {
        // "제목 — URL" 형식에서 URL 추출
        int sep = text.indexOf(" — ");
        QString url_part = (sep >= 0) ? text.mid(sep + 3) : text;
        url_input_->setText(url_part);
        onReturnPressed();
    });

    // 북마크 버튼 (오른쪽)
    bookmark_button_ = new QPushButton("☆", this);
    bookmark_button_->setFixedSize(28, 28);
    bookmark_button_->setCursor(Qt::PointingHandCursor);
    bookmark_button_->setStyleSheet(
        "QPushButton { color: #888; background: transparent; border: none; font-size: 16px; border-radius: 4px; }"
        "QPushButton:hover { color: #FFD700; background: #3a3a45; }"
    );
    bookmark_button_->setToolTip("북마크 추가");
    connect(bookmark_button_, &QPushButton::clicked, this, [this]() {
        emit bookmarkRequested(current_url_);
    });
    layout->addWidget(bookmark_button_);

    // 진행 바 (주소 바 하단 오버레이)
    progress_bar_ = new QProgressBar(this);
    progress_bar_->setFixedHeight(3);
    progress_bar_->setTextVisible(false);
    progress_bar_->setRange(0, 100);
    progress_bar_->setValue(0);
    progress_bar_->setStyleSheet(
        "QProgressBar { background: transparent; border: none; }"
        "QProgressBar::chunk { background: #4682dc; border-radius: 1px; }"
    );
    progress_bar_->setVisible(false);
}

void AddressBar::setUrl(const QString& url) {
    current_url_ = url;
    url_input_->setText(formatDisplayUrl(url));
}

QString AddressBar::url() const {
    return current_url_;
}

void AddressBar::setSecurityStatus(int status) {
    security_status_ = status;
    updateSecurityIcon();
}

void AddressBar::setLoadProgress(int percent) {
    load_progress_ = percent;
    updateProgressBar();
}

void AddressBar::setFocus() {
    url_input_->setFocus();
}

void AddressBar::selectAll() {
    url_input_->selectAll();
}

void AddressBar::addToHistory(const QString& url, const QString& title) {
    autocomplete_model_->addHistoryEntry(url, title);
}

void AddressBar::setSearchEngine(const QString& pattern) {
    search_engine_pattern_ = pattern;
}

void AddressBar::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
}

void AddressBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    // 진행 바 위치 조정 (주소 바 하단에 겹치기)
    if (progress_bar_) {
        int input_x = url_input_->x();
        int input_w = url_input_->width();
        int bar_y = height() - 3;
        progress_bar_->setGeometry(input_x, bar_y, input_w, 3);
    }
}

// ============================================================
// 슬롯
// ============================================================

void AddressBar::onReturnPressed() {
    QString input = url_input_->text().trimmed();
    if (input.isEmpty()) return;

    QString final_url;
    if (isLikelyUrl(input)) {
        final_url = normalizeUrl(input);
    } else {
        final_url = buildSearchUrl(input);
    }

    current_url_ = final_url;

    // 히스토리에 추가
    autocomplete_model_->addHistoryEntry(final_url, "");

    emit urlEntered(final_url);
}

void AddressBar::onTextChanged(const QString& text) {
    // 자동완성 제안 업데이트
    autocomplete_model_->updateSuggestions(text);
}

void AddressBar::onSecurityIconClicked() {
    emit securityIconClicked();
}

// ============================================================
// 내부 메서드
// ============================================================

bool AddressBar::isLikelyUrl(const QString& input) const {
    // 프로토콜 접두사가 있는 경우
    if (input.startsWith("http://") || input.startsWith("https://") ||
        input.startsWith("ftp://") || input.startsWith("ordinal://") ||
        input.startsWith("file://")) {
        return true;
    }

    // localhost 또는 IP 주소
    static QRegularExpression ip_regex(
        R"(^(\d{1,3}\.){3}\d{1,3}(:\d+)?(/.*)?$)");
    if (input.startsWith("localhost") || ip_regex.match(input).hasMatch()) {
        return true;
    }

    // 도메인 형태: 점(.) 포함, 공백 없음, 유효한 TLD
    static QRegularExpression domain_regex(
        R"(^[a-zA-Z0-9\-]+(\.[a-zA-Z0-9\-]+)+(\:[0-9]+)?(/.*)?$)");
    if (domain_regex.match(input).hasMatch()) {
        // 알려진 TLD 확인
        static const QStringList known_tlds = {
            "com", "org", "net", "io", "dev", "app", "co", "me",
            "kr", "jp", "uk", "de", "fr", "cn", "ru", "br",
            "edu", "gov", "mil", "int", "info", "biz", "name",
            "xyz", "tech", "ai", "eth", "btc"
        };

        // 마지막 점 이후의 문자열 추출
        int last_dot = input.lastIndexOf('.');
        if (last_dot >= 0) {
            QString tld = input.mid(last_dot + 1).split('/').first().split(':').first().toLower();
            if (known_tlds.contains(tld)) {
                return true;
            }
        }
    }

    return false;
}

QString AddressBar::buildSearchUrl(const QString& query) const {
    // 검색 엔진 단축키 확인
    static const QMap<QString, QString> search_shortcuts = {
        {"!g",  "https://www.google.com/search?q=%s"},
        {"!b",  "https://www.bing.com/search?q=%s"},
        {"!d",  "https://duckduckgo.com/?q=%s"},
        {"!y",  "https://search.yahoo.com/search?p=%s"},
        {"!w",  "https://en.wikipedia.org/w/index.php?search=%s"},
        {"!gh", "https://github.com/search?q=%s"},
        {"!yt", "https://www.youtube.com/results?search_query=%s"},
        {"!n",  "https://search.naver.com/search.naver?query=%s"},
    };

    QString search_query = query;
    QString engine_pattern = search_engine_pattern_;

    // 검색 단축키 처리
    for (auto it = search_shortcuts.begin(); it != search_shortcuts.end(); ++it) {
        if (query.startsWith(it.key() + " ")) {
            search_query = query.mid(it.key().length() + 1);
            engine_pattern = it.value();
            break;
        }
    }

    // %s를 URL 인코딩된 검색어로 대체
    QString encoded = QUrl::toPercentEncoding(search_query);
    return engine_pattern.replace("%s", encoded);
}

QString AddressBar::normalizeUrl(const QString& input) const {
    QString url = input.trimmed();

    // 스킴이 없으면 https:// 추가
    if (!url.contains("://")) {
        url = "https://" + url;
    }

    return url;
}

QString AddressBar::formatDisplayUrl(const QString& url) const {
    // ordinal:// 또는 특수 스킴은 그대로 표시
    if (url.startsWith("ordinal://") || url.startsWith("file://")) {
        return url;
    }

    // https:// 제거하여 깔끔하게 표시
    QString display = url;
    if (display.startsWith("https://")) {
        display = display.mid(8);
    } else if (display.startsWith("http://")) {
        display = display.mid(7);
    }

    // 후행 슬래시 제거
    if (display.endsWith('/') && display.count('/') == 0) {
        display.chop(1);
    }

    return display;
}

void AddressBar::updateSecurityIcon() {
    if (!security_icon_) return;

    switch (security_status_) {
        case 0: // 안전
            security_icon_->setText("🟢");
            security_icon_->setToolTip("보안 연결 (HTTPS)");
            break;
        case 1: // 경고
            security_icon_->setText("🟡");
            security_icon_->setToolTip("보안 경고: 혼합 콘텐츠 또는 약한 인증서");
            break;
        case 2: // 위험
            security_icon_->setText("🔴");
            security_icon_->setToolTip("위험: 피싱 또는 악성 사이트 의심");
            break;
        default:
            security_icon_->setText("⚪");
            security_icon_->setToolTip("보안 상태 알 수 없음");
            break;
    }
}

void AddressBar::updateProgressBar() {
    if (!progress_bar_) return;

    if (load_progress_ <= 0 || load_progress_ >= 100) {
        progress_bar_->setVisible(false);
        progress_bar_->setValue(0);
    } else {
        progress_bar_->setVisible(true);
        progress_bar_->setValue(load_progress_);
    }
}

} // namespace ordinal::ui
