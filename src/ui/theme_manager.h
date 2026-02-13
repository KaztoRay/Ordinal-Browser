/**
 * @file theme_manager.h
 * @brief 테마 관리자 — 라이트/다크/시스템 테마 전환 및 QPalette 생성
 * 
 * Catppuccin 컬러 팔레트 기반의 테마 시스템.
 * 다크: Mocha (#1e1e2e 배경, #cdd6f4 텍스트, #89b4fa 강조)
 * 라이트: Latte (#eff1f5 배경, #4c4f69 텍스트, #1e66f5 강조)
 * 
 * © 2026 KaztoRay — MIT License
 */

#ifndef ORDINAL_THEME_MANAGER_H
#define ORDINAL_THEME_MANAGER_H

#include <QObject>
#include <QPalette>
#include <QString>
#include <QColor>
#include <QSettings>

namespace Ordinal {

/**
 * @brief 테마 종류 열거형
 */
enum class ThemeMode {
    Light,   // 라이트 테마 (Catppuccin Latte)
    Dark,    // 다크 테마 (Catppuccin Mocha)
    System   // 시스템 설정 따라감
};

/**
 * @class ThemeManager
 * @brief 애플리케이션 전역 테마 관리자
 * 
 * QPalette 생성, QSS 스타일시트 생성, 사용자 커스텀 강조색 지원,
 * 핫 리로드(시그널 기반), QSettings 저장/로드 기능 제공.
 */
class ThemeManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 싱글턴 인스턴스 반환
     */
    static ThemeManager& instance();

    /**
     * @brief 현재 테마 모드 반환
     */
    ThemeMode currentTheme() const;

    /**
     * @brief 테마 변경
     * @param mode 적용할 테마 모드
     */
    void setTheme(ThemeMode mode);

    /**
     * @brief 현재 테마에 대한 QPalette 생성
     * @return 생성된 QPalette
     */
    QPalette generatePalette() const;

    /**
     * @brief 현재 테마에 대한 QSS 스타일시트 생성
     * @return QSS 문자열
     */
    QString generateStylesheet() const;

    /**
     * @brief 사용자 커스텀 강조색 설정
     * @param color 강조색 (무효하면 기본 강조색 사용)
     */
    void setCustomAccentColor(const QColor& color);

    /**
     * @brief 커스텀 강조색 초기화 (기본값 복원)
     */
    void resetAccentColor();

    /**
     * @brief 현재 강조색 반환
     */
    QColor accentColor() const;

    /**
     * @brief QSettings에서 테마 설정 로드
     */
    void loadFromSettings();

    /**
     * @brief QSettings에 테마 설정 저장
     */
    void saveToSettings() const;

    /**
     * @brief 시스템이 다크 모드인지 감지
     */
    static bool isSystemDarkMode();

    /**
     * @brief 실제 적용되는 테마 (System일 경우 해석된 결과)
     */
    ThemeMode resolvedTheme() const;

    // 다크 테마 색상 상수 (Catppuccin Mocha)
    static constexpr const char* DARK_BG        = "#1e1e2e";
    static constexpr const char* DARK_BG_ALT    = "#181825";
    static constexpr const char* DARK_SURFACE   = "#313244";
    static constexpr const char* DARK_OVERLAY    = "#45475a";
    static constexpr const char* DARK_TEXT       = "#cdd6f4";
    static constexpr const char* DARK_SUBTEXT    = "#a6adc8";
    static constexpr const char* DARK_ACCENT     = "#89b4fa";
    static constexpr const char* DARK_RED        = "#f38ba8";
    static constexpr const char* DARK_GREEN      = "#a6e3a1";
    static constexpr const char* DARK_YELLOW     = "#f9e2af";

    // 라이트 테마 색상 상수 (Catppuccin Latte)
    static constexpr const char* LIGHT_BG       = "#eff1f5";
    static constexpr const char* LIGHT_BG_ALT   = "#e6e9ef";
    static constexpr const char* LIGHT_SURFACE  = "#ccd0da";
    static constexpr const char* LIGHT_OVERLAY   = "#bcc0cc";
    static constexpr const char* LIGHT_TEXT      = "#4c4f69";
    static constexpr const char* LIGHT_SUBTEXT   = "#6c6f85";
    static constexpr const char* LIGHT_ACCENT    = "#1e66f5";
    static constexpr const char* LIGHT_RED       = "#d20f39";
    static constexpr const char* LIGHT_GREEN     = "#40a02b";
    static constexpr const char* LIGHT_YELLOW    = "#df8e1d";

signals:
    /**
     * @brief 테마가 변경될 때 발생하는 시그널 (핫 리로드용)
     * @param mode 새로운 테마 모드
     */
    void themeChanged(ThemeMode mode);

    /**
     * @brief 강조색이 변경될 때 발생하는 시그널
     * @param color 새로운 강조색
     */
    void accentColorChanged(const QColor& color);

private:
    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() = default;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    /**
     * @brief 다크 테마 팔레트 생성
     */
    QPalette buildDarkPalette() const;

    /**
     * @brief 라이트 테마 팔레트 생성
     */
    QPalette buildLightPalette() const;

    /**
     * @brief 다크 테마 QSS 생성
     */
    QString buildDarkStylesheet() const;

    /**
     * @brief 라이트 테마 QSS 생성
     */
    QString buildLightStylesheet() const;

    ThemeMode m_currentTheme;       ///< 현재 테마 모드
    QColor m_customAccent;          ///< 사용자 커스텀 강조색 (무효하면 기본값)
};

} // namespace Ordinal

#endif // ORDINAL_THEME_MANAGER_H
