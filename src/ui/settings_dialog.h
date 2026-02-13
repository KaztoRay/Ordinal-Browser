/**
 * @file settings_dialog.h
 * @brief 설정 다이얼로그 — 6개 탭으로 구성된 브라우저 환경 설정
 * 
 * 탭 구성:
 *   1. 일반 — 홈페이지, 시작 동작, 검색 엔진, 북마크바
 *   2. 프라이버시 — 쿠키 정책, DNT, 비밀번호 관리자, 자동완성, 데이터 삭제
 *   3. 보안 — 위협 감도, 피싱/XSS 보호, 추적기/광고 차단, 인증서
 *   4. 외관 — 테마, 확대/축소, 글꼴, 상태바, 툴바 스타일
 *   5. 확장 — 확장 목록, 로드/삭제, 권한 상세
 *   6. 고급 — 프록시, 캐시, 개발자 도구, 하드웨어 가속, 실험 기능, 초기화
 * 
 * © 2026 KaztoRay — MIT License
 */

#ifndef ORDINAL_SETTINGS_DIALOG_H
#define ORDINAL_SETTINGS_DIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QListWidget>
#include <QPushButton>
#include <QFontComboBox>
#include <QLabel>
#include <QSettings>
#include <QDialogButtonBox>

namespace Ordinal {

/**
 * @class SettingsDialog
 * @brief 브라우저 설정 다이얼로그 (6개 탭, Apply/OK/Cancel)
 */
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() = default;

signals:
    /**
     * @brief 설정이 적용될 때 발생
     */
    void settingsApplied();

private slots:
    /**
     * @brief Apply 버튼 클릭 시 — 설정 저장 후 시그널 발생
     */
    void onApply();

    /**
     * @brief OK 버튼 클릭 시 — 적용 후 닫기
     */
    void onOk();

    /**
     * @brief 데이터 삭제 버튼 클릭 시 — 삭제 다이얼로그 표시
     */
    void onClearData();

    /**
     * @brief 확장 로드 버튼 클릭
     */
    void onLoadExtension();

    /**
     * @brief 확장 삭제 버튼 클릭
     */
    void onRemoveExtension();

    /**
     * @brief 확장 선택 변경 시 — 권한 상세 업데이트
     */
    void onExtensionSelected();

    /**
     * @brief 모든 설정 초기화
     */
    void onResetSettings();

private:
    // ---- 탭 생성 메서드 ----
    QWidget* createGeneralTab();
    QWidget* createPrivacyTab();
    QWidget* createSecurityTab();
    QWidget* createAppearanceTab();
    QWidget* createExtensionsTab();
    QWidget* createAdvancedTab();

    /**
     * @brief QSettings에서 현재 설정 로드하여 위젯에 반영
     */
    void loadSettings();

    /**
     * @brief 위젯 값을 QSettings에 저장
     */
    void saveSettings();

    // ---- UI 구성 요소 ----
    QTabWidget* m_tabWidget;              ///< 메인 탭 위젯
    QDialogButtonBox* m_buttonBox;        ///< OK/Apply/Cancel 버튼

    // 일반 탭
    QLineEdit*  m_homepageEdit;           ///< 홈페이지 URL
    QComboBox*  m_startupCombo;           ///< 시작 동작 (빈 페이지/복원/홈)
    QComboBox*  m_searchEngineCombo;      ///< 기본 검색 엔진
    QCheckBox*  m_bookmarksBarCheck;      ///< 북마크바 표시 여부

    // 프라이버시 탭
    QComboBox*  m_cookiePolicyCombo;      ///< 쿠키 정책
    QCheckBox*  m_dntCheck;               ///< Do Not Track
    QCheckBox*  m_passwordManagerCheck;   ///< 비밀번호 관리자 활성화
    QCheckBox*  m_autofillCheck;          ///< 자동완성 활성화
    QPushButton* m_clearDataBtn;          ///< 데이터 삭제 버튼

    // 보안 탭
    QSlider*    m_threatSlider;           ///< 위협 감도 (1-5)
    QLabel*     m_threatLabel;            ///< 현재 감도 표시
    QCheckBox*  m_phishingCheck;          ///< 피싱 보호
    QCheckBox*  m_xssCheck;              ///< XSS 보호
    QCheckBox*  m_trackerBlockCheck;      ///< 추적기 차단
    QCheckBox*  m_adBlockCheck;           ///< 광고 차단
    QCheckBox*  m_strictCertsCheck;       ///< 엄격한 인증서 검증

    // 외관 탭
    QComboBox*      m_themeCombo;         ///< 테마 선택
    QSpinBox*       m_zoomSpin;           ///< 확대/축소 (%)
    QFontComboBox*  m_fontCombo;          ///< 기본 글꼴
    QCheckBox*      m_statusBarCheck;     ///< 상태바 표시
    QComboBox*      m_toolbarStyleCombo;  ///< 툴바 스타일

    // 확장 탭
    QListWidget*  m_extensionsList;       ///< 확장 목록
    QPushButton*  m_loadExtBtn;           ///< 확장 로드 버튼
    QPushButton*  m_removeExtBtn;         ///< 확장 삭제 버튼
    QLabel*       m_extPermissionsLabel;  ///< 선택된 확장의 권한 상세

    // 고급 탭
    QLineEdit*  m_proxyEdit;              ///< 프록시 주소
    QSpinBox*   m_cacheSizeSpin;          ///< 캐시 크기 (MB)
    QCheckBox*  m_devToolsCheck;          ///< 개발자 도구 활성화
    QCheckBox*  m_hardwareAccelCheck;     ///< 하드웨어 가속
    QCheckBox*  m_experimentalCheck;      ///< 실험 기능
    QPushButton* m_resetBtn;              ///< 설정 초기화 버튼
};

} // namespace Ordinal

#endif // ORDINAL_SETTINGS_DIALOG_H
