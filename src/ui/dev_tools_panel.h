#pragma once

/**
 * @file dev_tools_panel.h
 * @brief 개발자 도구 패널
 * 
 * QWidget + QTabWidget 기반 개발자 도구.
 * 콘솔 탭 (V8 eval), 네트워크 탭 (요청 워터폴),
 * 보안 탭 (취약점 스캔), 요소 탭 (DOM 트리)을 제공합니다.
 */

#include <QWidget>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QTableView>
#include <QTreeView>
#include <QTreeWidget>
#include <QStandardItemModel>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QComboBox>
#include <QTimer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QProgressBar>

#include <vector>
#include <deque>
#include <functional>

namespace ordinal::ui {

/**
 * @brief 콘솔 메시지 레벨
 */
enum class ConsoleLevel {
    Log,     ///< 일반 로그
    Info,    ///< 정보
    Warn,    ///< 경고
    Error,   ///< 오류
    Debug,   ///< 디버그
    Result   ///< 실행 결과
};

/**
 * @brief 콘솔 메시지 항목
 */
struct ConsoleMessage {
    ConsoleLevel level;
    QString text;
    QString source;    ///< 소스 파일:라인
    QDateTime timestamp;
};

// ============================================================

/**
 * @brief 네트워크 요청 항목
 */
struct NetworkRequest {
    int id{0};
    QString method;        ///< GET, POST 등
    QString url;
    int status_code{0};
    QString content_type;
    qint64 size_bytes{0};
    double time_ms{0.0};   ///< 총 소요 시간
    double dns_ms{0.0};    ///< DNS 조회 시간
    double connect_ms{0.0}; ///< 연결 시간
    double ssl_ms{0.0};    ///< SSL 핸드셰이크 시간
    double ttfb_ms{0.0};   ///< Time to First Byte
    double download_ms{0.0}; ///< 다운로드 시간
    bool blocked{false};    ///< 차단 여부
    QString initiator;      ///< 요청 발생 소스
    QDateTime timestamp;
};

// ============================================================

/**
 * @brief 보안 스캔 결과 항목
 */
struct SecurityFinding {
    QString category;      ///< XSS, CSRF, 혼합 콘텐츠 등
    QString severity;      ///< Critical, High, Medium, Low, Info
    QString description;
    QString element;       ///< 관련 DOM 요소
    QString recommendation; ///< 권장 조치
};

// ============================================================

/**
 * @brief 콘솔 탭 위젯
 * 
 * V8 JavaScript 실행 콘솔.
 * 입력/출력 로그, 명령어 히스토리, 자동완성을 지원합니다.
 */
class ConsoleTab : public QWidget {
    Q_OBJECT

public:
    explicit ConsoleTab(QWidget* parent = nullptr);

    /**
     * @brief 콘솔 메시지 추가
     */
    void addMessage(const ConsoleMessage& msg);

    /**
     * @brief 콘솔 초기화
     */
    void clear();

    /**
     * @brief 로그 필터 레벨 설정
     */
    void setFilterLevel(ConsoleLevel min_level);

signals:
    /**
     * @brief JavaScript 코드 실행 요청
     */
    void executeRequested(const QString& code);

private slots:
    void onInputSubmit();

private:
    void appendFormattedMessage(const ConsoleMessage& msg);
    QString levelPrefix(ConsoleLevel level) const;
    QColor levelColor(ConsoleLevel level) const;

    QPlainTextEdit* output_{nullptr};
    QLineEdit* input_{nullptr};
    QComboBox* filter_combo_{nullptr};
    QPushButton* clear_button_{nullptr};

    std::deque<ConsoleMessage> messages_;
    std::vector<QString> command_history_;
    int history_index_{-1};
    ConsoleLevel filter_level_{ConsoleLevel::Log};

    static constexpr int MAX_MESSAGES = 5000;
    static constexpr int MAX_HISTORY = 200;
};

// ============================================================

/**
 * @brief 네트워크 탭 위젯
 * 
 * HTTP 요청/응답 목록과 워터폴 타이밍 표시.
 */
class NetworkTab : public QWidget {
    Q_OBJECT

public:
    explicit NetworkTab(QWidget* parent = nullptr);

    /**
     * @brief 네트워크 요청 추가
     */
    void addRequest(const NetworkRequest& request);

    /**
     * @brief 요청 목록 초기화
     */
    void clear();

    /**
     * @brief 필터 설정
     */
    void setFilter(const QString& filter);

signals:
    void requestSelected(int id);

private:
    void updateSummary();
    QString formatSize(qint64 bytes) const;
    QString formatTime(double ms) const;

    QTableView* table_view_{nullptr};
    QStandardItemModel* table_model_{nullptr};
    QLineEdit* filter_input_{nullptr};
    QPushButton* clear_button_{nullptr};
    QLabel* summary_label_{nullptr};
    QComboBox* type_filter_{nullptr};

    std::vector<NetworkRequest> requests_;
    int next_id_{1};
};

// ============================================================

/**
 * @brief 보안 탭 위젯
 * 
 * 페이지 보안 취약점 스캔 결과를 표시합니다.
 */
class SecurityTab : public QWidget {
    Q_OBJECT

public:
    explicit SecurityTab(QWidget* parent = nullptr);

    /**
     * @brief 스캔 결과 추가
     */
    void addFinding(const SecurityFinding& finding);

    /**
     * @brief 결과 초기화
     */
    void clear();

    /**
     * @brief 스캔 진행률 설정
     */
    void setScanProgress(int percent);

signals:
    void scanRequested();
    void findingSelected(int index);

private:
    QTreeWidget* findings_tree_{nullptr};
    QPushButton* scan_button_{nullptr};
    QProgressBar* scan_progress_{nullptr};
    QLabel* status_label_{nullptr};
    QLabel* summary_label_{nullptr};

    std::vector<SecurityFinding> findings_;
};

// ============================================================

/**
 * @brief 요소 탭 위젯
 * 
 * DOM 트리를 QTreeView로 표시합니다.
 */
class ElementsTab : public QWidget {
    Q_OBJECT

public:
    explicit ElementsTab(QWidget* parent = nullptr);

    /**
     * @brief DOM 트리 데이터 설정
     * @param html_source HTML 소스 코드
     */
    void setDomTree(const QString& html_source);

    /**
     * @brief DOM 노드 추가
     * @param tag 태그명
     * @param attributes 속성 문자열
     * @param parent_path 부모 경로 (빈 문자열이면 루트)
     */
    void addNode(const QString& tag, const QString& attributes,
                 const QString& parent_path = "");

    /**
     * @brief 트리 초기화
     */
    void clear();

signals:
    void nodeSelected(const QString& path);

private:
    void parseHtmlToTree(const QString& html);
    QTreeWidgetItem* findNodeByPath(const QString& path);

    QTreeWidget* dom_tree_{nullptr};
    QPlainTextEdit* source_view_{nullptr};
    QSplitter* splitter_{nullptr};
    QLineEdit* search_input_{nullptr};
};

// ============================================================

/**
 * @brief 개발자 도구 패널 (메인 위젯)
 */
class DevToolsPanel : public QWidget {
    Q_OBJECT

public:
    explicit DevToolsPanel(QWidget* parent = nullptr);
    ~DevToolsPanel() override = default;

    // 탭 접근자
    [[nodiscard]] ConsoleTab* consoleTab() const { return console_tab_; }
    [[nodiscard]] NetworkTab* networkTab() const { return network_tab_; }
    [[nodiscard]] SecurityTab* securityTab() const { return security_tab_; }
    [[nodiscard]] ElementsTab* elementsTab() const { return elements_tab_; }

    /**
     * @brief 특정 탭으로 전환
     */
    void showTab(int index);

signals:
    void panelClosed();

private:
    QTabWidget* tab_widget_{nullptr};
    ConsoleTab* console_tab_{nullptr};
    NetworkTab* network_tab_{nullptr};
    SecurityTab* security_tab_{nullptr};
    ElementsTab* elements_tab_{nullptr};
    QPushButton* close_button_{nullptr};
};

} // namespace ordinal::ui
