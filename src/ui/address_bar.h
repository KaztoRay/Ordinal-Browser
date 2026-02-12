#pragma once

/**
 * @file address_bar.h
 * @brief 주소 바 위젯
 * 
 * URL 입력, 자동완성 팝업 (히스토리 기반),
 * 보안 잠금 아이콘 (🟢🟡🔴), 검색 엔진 통합,
 * 페이지 로드 진행 바 오버레이를 제공합니다.
 */

#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QCompleter>
#include <QStringListModel>
#include <QListView>
#include <QTimer>
#include <QPaintEvent>
#include <QAction>

#include <vector>
#include <string>

namespace ordinal::ui {

/**
 * @brief 자동완성 팝업 모델
 * 
 * 방문 히스토리와 북마크를 기반으로
 * URL 자동완성 제안을 제공합니다.
 */
class AutocompleteModel : public QStringListModel {
    Q_OBJECT

public:
    explicit AutocompleteModel(QObject* parent = nullptr);

    /**
     * @brief 히스토리 항목 추가
     */
    void addHistoryEntry(const QString& url, const QString& title);

    /**
     * @brief 입력 텍스트 기반 제안 목록 업데이트
     */
    void updateSuggestions(const QString& input);

    /**
     * @brief 히스토리 초기화
     */
    void clearHistory();

    /**
     * @brief 히스토리 크기
     */
    [[nodiscard]] int historySize() const { return static_cast<int>(history_.size()); }

private:
    // 히스토리 엔트리
    struct HistoryEntry {
        QString url;          ///< URL
        QString title;        ///< 페이지 제목
        int visit_count{1};   ///< 방문 횟수
        qint64 last_visit{0}; ///< 마지막 방문 시각
    };

    std::vector<HistoryEntry> history_;
    static constexpr int MAX_SUGGESTIONS = 8; ///< 최대 제안 수
    static constexpr int MAX_HISTORY = 1000;  ///< 최대 히스토리 수
};

// ============================================================

/**
 * @brief 주소 바 위젯
 * 
 * URL 입력 필드, 보안 아이콘, 자동완성,
 * 검색 엔진 통합, 진행 바 오버레이를 포함합니다.
 */
class AddressBar : public QWidget {
    Q_OBJECT

public:
    explicit AddressBar(QWidget* parent = nullptr);
    ~AddressBar() override = default;

    // ============================
    // URL 관리
    // ============================

    /**
     * @brief 현재 URL 설정
     */
    void setUrl(const QString& url);

    /**
     * @brief 현재 URL 반환
     */
    [[nodiscard]] QString url() const;

    /**
     * @brief 보안 상태 설정 (0=안전, 1=경고, 2=위험)
     */
    void setSecurityStatus(int status);

    /**
     * @brief 로딩 진행률 설정 (0-100, 0이면 숨김)
     */
    void setLoadProgress(int percent);

    /**
     * @brief 입력 포커스 설정
     */
    void setFocus();

    /**
     * @brief 텍스트 전체 선택
     */
    void selectAll();

    /**
     * @brief 히스토리 항목 추가
     */
    void addToHistory(const QString& url, const QString& title);

    // ============================
    // 검색 엔진 설정
    // ============================

    /**
     * @brief 기본 검색 엔진 URL 패턴 설정
     * @param pattern 검색 URL (%s가 검색어로 대체됨)
     */
    void setSearchEngine(const QString& pattern);

signals:
    /**
     * @brief URL 입력 완료 시그널 (Enter 키)
     */
    void urlEntered(const QString& url);

    /**
     * @brief 보안 아이콘 클릭 시그널
     */
    void securityIconClicked();

    /**
     * @brief 북마크 버튼 클릭
     */
    void bookmarkRequested(const QString& url);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onReturnPressed();
    void onTextChanged(const QString& text);
    void onSecurityIconClicked();

private:
    /**
     * @brief 입력이 URL인지 검색어인지 판별
     * @return true면 URL, false면 검색어
     */
    [[nodiscard]] bool isLikelyUrl(const QString& input) const;

    /**
     * @brief 검색어를 검색 엔진 URL로 변환
     */
    [[nodiscard]] QString buildSearchUrl(const QString& query) const;

    /**
     * @brief URL 정규화 (스킴 추가 등)
     */
    [[nodiscard]] QString normalizeUrl(const QString& input) const;

    /**
     * @brief 표시용 URL 포맷 (스킴 생략 등)
     */
    [[nodiscard]] QString formatDisplayUrl(const QString& url) const;

    void updateSecurityIcon();
    void updateProgressBar();

    // UI 컴포넌트
    QLineEdit* url_input_{nullptr};
    QLabel* security_icon_{nullptr};
    QPushButton* bookmark_button_{nullptr};
    QProgressBar* progress_bar_{nullptr};

    // 자동완성
    QCompleter* completer_{nullptr};
    AutocompleteModel* autocomplete_model_{nullptr};

    // 상태
    int security_status_{0}; // 0=안전, 1=경고, 2=위험
    int load_progress_{0};
    QString current_url_;
    QString search_engine_pattern_{"https://www.google.com/search?q=%s"};
};

} // namespace ordinal::ui
