#pragma once

/**
 * @file security_panel.h
 * @brief 보안 패널 위젯
 * 
 * QDockWidget 기반 보안 정보 패널.
 * 실시간 위협 알림 목록 (심각도별 정렬), 보안 점수 게이지 (0-100),
 * 차단된 트래커 카운터, 인증서 정보 카드, 페이지별 보안 요약을 표시합니다.
 */

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QGroupBox>
#include <QScrollArea>
#include <QPainter>
#include <QTimer>
#include <QDateTime>
#include <QHeaderView>
#include <QTreeWidget>

#include <vector>
#include <string>

// 전방 선언
namespace ordinal::ui { enum class SecurityStatus; }

namespace ordinal::ui {

/**
 * @brief 위협 심각도 레벨
 */
enum class ThreatSeverity {
    Info,       ///< 정보
    Low,        ///< 낮음
    Medium,     ///< 보통
    High,       ///< 높음
    Critical    ///< 심각
};

/**
 * @brief 위협 알림 항목
 */
struct ThreatAlert {
    QString title;              ///< 위협 제목
    QString description;        ///< 상세 설명
    ThreatSeverity severity;    ///< 심각도
    QString source;             ///< 출처 (URL/스크립트)
    QDateTime timestamp;        ///< 발생 시각
    bool resolved{false};       ///< 해결 여부
};

/**
 * @brief 인증서 정보
 */
struct CertificateInfo {
    QString subject;            ///< 인증서 주체 (CN)
    QString issuer;             ///< 발급 기관
    QString serial_number;      ///< 일련번호
    QString fingerprint_sha256; ///< SHA-256 지문
    QDateTime valid_from;       ///< 유효 기간 시작
    QDateTime valid_until;      ///< 유효 기간 종료
    bool is_ev{false};          ///< EV 인증서 여부
    int key_bits{0};            ///< 키 비트 수
    QString protocol;           ///< TLS 프로토콜 버전
    QString cipher_suite;       ///< 암호화 스위트
};

// ============================================================

/**
 * @brief 보안 점수 게이지 위젯
 * 
 * 원형 게이지로 보안 점수 (0-100)를 시각적으로 표시합니다.
 * 색상은 점수에 따라 그라데이션으로 변화합니다.
 */
class SecurityGauge : public QWidget {
    Q_OBJECT

public:
    explicit SecurityGauge(QWidget* parent = nullptr);

    void setScore(int score);
    [[nodiscard]] int score() const { return score_; }

    void setAnimated(bool animated);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int score_{100};
    int display_score_{100}; // 애니메이션용
    QTimer* anim_timer_{nullptr};
    bool animated_{true};

    /**
     * @brief 점수에 따른 색상 반환
     */
    [[nodiscard]] QColor scoreColor(int score) const;
};

// ============================================================

/**
 * @brief 보안 패널 (QWidget 기반, 메인 윈도우에서 QSplitter에 배치)
 */
class SecurityPanel : public QWidget {
    Q_OBJECT

public:
    explicit SecurityPanel(QWidget* parent = nullptr);
    ~SecurityPanel() override = default;

    // ============================
    // 보안 상태 업데이트
    // ============================

    /**
     * @brief 전체 보안 상태 업데이트
     */
    void updateStatus(SecurityStatus status, const QString& message);

    /**
     * @brief 보안 점수 설정
     */
    void setSecurityScore(int score);

    /**
     * @brief 위협 알림 추가
     */
    void addThreatAlert(const ThreatAlert& alert);

    /**
     * @brief 위협 알림 초기화
     */
    void clearAlerts();

    /**
     * @brief 차단된 트래커 수 설정
     */
    void setBlockedTrackers(int count);

    /**
     * @brief 차단된 트래커 추가
     */
    void addBlockedTracker(const QString& name, const QString& domain);

    /**
     * @brief 인증서 정보 설정
     */
    void setCertificateInfo(const CertificateInfo& cert);

    /**
     * @brief 페이지 보안 요약 설정
     */
    void setPageSummary(const QString& url, int score, int threats, int trackers);

signals:
    void alertClicked(int index);
    void rescanRequested();

private:
    void setupUi();
    void setupScoreSection();
    void setupAlertSection();
    void setupTrackerSection();
    void setupCertSection();
    void setupSummarySection();

    void updateAlertTable();
    QString severityText(ThreatSeverity severity) const;
    QColor severityColor(ThreatSeverity severity) const;
    QString severityIcon(ThreatSeverity severity) const;

    // 레이아웃
    QVBoxLayout* main_layout_{nullptr};
    QScrollArea* scroll_area_{nullptr};
    QWidget* content_widget_{nullptr};

    // 보안 점수 섹션
    SecurityGauge* security_gauge_{nullptr};
    QLabel* score_label_{nullptr};
    QLabel* score_description_{nullptr};

    // 위협 알림 섹션
    QGroupBox* alert_group_{nullptr};
    QTableWidget* alert_table_{nullptr};
    QLabel* alert_count_label_{nullptr};
    std::vector<ThreatAlert> alerts_;

    // 트래커 차단 섹션
    QGroupBox* tracker_group_{nullptr};
    QLabel* tracker_count_label_{nullptr};
    QTreeWidget* tracker_tree_{nullptr};
    int blocked_tracker_count_{0};

    // 인증서 정보 섹션
    QGroupBox* cert_group_{nullptr};
    QLabel* cert_subject_label_{nullptr};
    QLabel* cert_issuer_label_{nullptr};
    QLabel* cert_validity_label_{nullptr};
    QLabel* cert_protocol_label_{nullptr};
    QLabel* cert_fingerprint_label_{nullptr};

    // 페이지 요약 섹션
    QGroupBox* summary_group_{nullptr};
    QLabel* summary_url_label_{nullptr};
    QLabel* summary_score_label_{nullptr};
    QLabel* summary_threats_label_{nullptr};
    QLabel* summary_trackers_label_{nullptr};
    QPushButton* rescan_button_{nullptr};
};

} // namespace ordinal::ui
