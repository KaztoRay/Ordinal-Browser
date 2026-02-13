/**
 * @file about_dialog.h
 * @brief 정보 다이얼로그 — 앱 아이콘, 버전, 빌드 정보, 라이선스, 업데이트 확인
 * 
 * Ordinal Browser v1.1.0 정보 표시 다이얼로그.
 * "V8 기반 보안 브라우저 + LLM Security Agent" 소개.
 * 
 * © 2026 KaztoRay — MIT License
 */

#ifndef ORDINAL_ABOUT_DIALOG_H
#define ORDINAL_ABOUT_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>

namespace Ordinal {

/**
 * @class AboutDialog
 * @brief Ordinal Browser 정보 다이얼로그
 * 
 * 중앙 정렬 레이아웃:
 *   - 앱 아이콘 (128px)
 *   - "Ordinal Browser" 제목 (24pt bold)
 *   - "v1.1.0" 버전
 *   - 부제목 설명
 *   - 빌드 정보 (Qt 버전, 플랫폼, 아키텍처)
 *   - 저작권 & 라이선스
 *   - GitHub 링크 버튼
 *   - 업데이트 확인 버튼
 *   - 서드파티 라이선스 텍스트 브라우저
 */
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
    ~AboutDialog() = default;

private slots:
    /**
     * @brief GitHub 저장소를 기본 브라우저로 열기
     */
    void onOpenGitHub();

    /**
     * @brief 업데이트 확인 (GitHub Releases API 조회)
     */
    void onCheckUpdate();

private:
    /**
     * @brief UI 레이아웃 구성
     */
    void setupUI();

    /**
     * @brief 빌드 정보 문자열 생성
     */
    QString buildInfoString() const;

    /**
     * @brief 서드파티 라이선스 텍스트 생성
     */
    QString thirdPartyLicensesText() const;

    // UI 요소
    QLabel*       m_iconLabel;        ///< 앱 아이콘 (128x128)
    QLabel*       m_titleLabel;       ///< "Ordinal Browser" 제목
    QLabel*       m_versionLabel;     ///< 버전 번호
    QLabel*       m_subtitleLabel;    ///< 부제목 설명
    QLabel*       m_buildInfoLabel;   ///< 빌드 정보
    QLabel*       m_copyrightLabel;   ///< 저작권
    QPushButton*  m_githubBtn;        ///< GitHub 버튼
    QPushButton*  m_updateBtn;        ///< 업데이트 확인 버튼
    QTextBrowser* m_licensesBrowser;  ///< 서드파티 라이선스
};

} // namespace Ordinal

#endif // ORDINAL_ABOUT_DIALOG_H
