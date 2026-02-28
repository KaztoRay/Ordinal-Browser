/**
 * @file theme_manager.cpp
 * @brief 테마 관리자 구현 — QPalette/QSS 생성, 핫 리로드, QSettings 연동
 * 
 * Catppuccin Mocha(다크) / Latte(라이트) 팔레트 기반.
 * 시스템 테마 자동 감지, 커스텀 강조색 오버라이드, 실시간 테마 전환.
 * 
 * © 2026 KaztoRay — MIT License
 */

#include "theme_manager.h"
#include <QApplication>
#include <QStyleHints>
#include <QSettings>
#include <QDebug>

namespace Ordinal {

// ============================================================
// 싱글턴 인스턴스
// ============================================================

ThemeManager& ThemeManager::instance() {
    static ThemeManager s_instance;
    return s_instance;
}

// ============================================================
// 생성자 — 기본 시스템 테마로 초기화
// ============================================================

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
    , m_currentTheme(ThemeMode::System)
    , m_customAccent()  // 무효 QColor → 기본 강조색 사용
{
    // QSettings에서 저장된 설정 로드 시도
    loadFromSettings();
    qDebug() << "[ThemeManager] 초기화 완료, 테마:" << static_cast<int>(m_currentTheme);
}

// ============================================================
// 테마 조회 / 변경
// ============================================================

ThemeMode ThemeManager::currentTheme() const {
    return m_currentTheme;
}

void ThemeManager::setTheme(ThemeMode mode) {
    if (m_currentTheme == mode) return;

    m_currentTheme = mode;
    saveToSettings();

    qDebug() << "[ThemeManager] 테마 변경됨:" << static_cast<int>(mode);

    // 핫 리로드 — 시그널로 전체 UI에 알림
    emit themeChanged(mode);
}

ThemeMode ThemeManager::resolvedTheme() const {
    if (m_currentTheme == ThemeMode::System) {
        return isSystemDarkMode() ? ThemeMode::Dark : ThemeMode::Light;
    }
    return m_currentTheme;
}

// ============================================================
// 시스템 다크 모드 감지
// ============================================================

bool ThemeManager::isSystemDarkMode() {
    // Qt 6.5+ 지원: QStyleHints로 시스템 색상 스킴 감지
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto scheme = QApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark;
#else
    // 폴백: 배경색 밝기로 추정
    QPalette defaultPalette;
    QColor bg = defaultPalette.color(QPalette::Window);
    return bg.lightnessF() < 0.5;
#endif
}

// ============================================================
// 강조색 관리
// ============================================================

void ThemeManager::setCustomAccentColor(const QColor& color) {
    if (!color.isValid()) return;

    m_customAccent = color;
    saveToSettings();

    qDebug() << "[ThemeManager] 커스텀 강조색 설정:" << color.name();
    emit accentColorChanged(color);
    // 테마 변경 시그널도 발생시켜 전체 스타일 갱신
    emit themeChanged(m_currentTheme);
}

void ThemeManager::resetAccentColor() {
    m_customAccent = QColor();  // 무효화 → 기본값 복원
    saveToSettings();

    QColor defaultAccent = (resolvedTheme() == ThemeMode::Dark)
        ? QColor(DARK_ACCENT) : QColor(LIGHT_ACCENT);

    qDebug() << "[ThemeManager] 강조색 초기화됨";
    emit accentColorChanged(defaultAccent);
    emit themeChanged(m_currentTheme);
}

QColor ThemeManager::accentColor() const {
    if (m_customAccent.isValid()) {
        return m_customAccent;
    }
    return (resolvedTheme() == ThemeMode::Dark)
        ? QColor(DARK_ACCENT) : QColor(LIGHT_ACCENT);
}

// ============================================================
// QPalette 생성
// ============================================================

QPalette ThemeManager::generatePalette() const {
    return (resolvedTheme() == ThemeMode::Dark)
        ? buildDarkPalette() : buildLightPalette();
}

QPalette ThemeManager::buildDarkPalette() const {
    QPalette palette;
    QColor accent = accentColor();

    // 기본 배경/전경
    palette.setColor(QPalette::Window,          QColor(DARK_BG));
    palette.setColor(QPalette::WindowText,      QColor(DARK_TEXT));
    palette.setColor(QPalette::Base,            QColor(DARK_BG_ALT));
    palette.setColor(QPalette::AlternateBase,   QColor(DARK_SURFACE));
    palette.setColor(QPalette::Text,            QColor(DARK_TEXT));
    palette.setColor(QPalette::BrightText,      QColor(DARK_RED));

    // 버튼
    palette.setColor(QPalette::Button,          QColor(DARK_SURFACE));
    palette.setColor(QPalette::ButtonText,      QColor(DARK_TEXT));

    // 강조색 (하이라이트)
    palette.setColor(QPalette::Highlight,       accent);
    palette.setColor(QPalette::HighlightedText, QColor(DARK_BG));

    // 툴팁
    palette.setColor(QPalette::ToolTipBase,     QColor(DARK_SURFACE));
    palette.setColor(QPalette::ToolTipText,     QColor(DARK_TEXT));

    // 링크
    palette.setColor(QPalette::Link,            accent);
    palette.setColor(QPalette::LinkVisited,     QColor("#b4befe"));  // Lavender

    // 비활성 상태 — 서브텍스트 색상으로 약화
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(DARK_OVERLAY));
    palette.setColor(QPalette::Disabled, QPalette::Text,       QColor(DARK_OVERLAY));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(DARK_OVERLAY));

    // 플레이스홀더 텍스트
    palette.setColor(QPalette::PlaceholderText, QColor(DARK_SUBTEXT));

    // 미드라인, 섀도우 등
    palette.setColor(QPalette::Mid,     QColor(DARK_SURFACE));
    palette.setColor(QPalette::Dark,    QColor(DARK_BG_ALT));
    palette.setColor(QPalette::Shadow,  QColor("#11111b"));
    palette.setColor(QPalette::Light,   QColor(DARK_OVERLAY));
    palette.setColor(QPalette::Midlight, QColor(DARK_SURFACE));

    return palette;
}

QPalette ThemeManager::buildLightPalette() const {
    QPalette palette;
    QColor accent = accentColor();

    // 기본 배경/전경
    palette.setColor(QPalette::Window,          QColor(LIGHT_BG));
    palette.setColor(QPalette::WindowText,      QColor(LIGHT_TEXT));
    palette.setColor(QPalette::Base,            QColor("#ffffff"));
    palette.setColor(QPalette::AlternateBase,   QColor(LIGHT_BG_ALT));
    palette.setColor(QPalette::Text,            QColor(LIGHT_TEXT));
    palette.setColor(QPalette::BrightText,      QColor(LIGHT_RED));

    // 버튼
    palette.setColor(QPalette::Button,          QColor(LIGHT_SURFACE));
    palette.setColor(QPalette::ButtonText,      QColor(LIGHT_TEXT));

    // 강조색
    palette.setColor(QPalette::Highlight,       accent);
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));

    // 툴팁
    palette.setColor(QPalette::ToolTipBase,     QColor(LIGHT_BG_ALT));
    palette.setColor(QPalette::ToolTipText,     QColor(LIGHT_TEXT));

    // 링크
    palette.setColor(QPalette::Link,            accent);
    palette.setColor(QPalette::LinkVisited,     QColor("#7287fd"));  // Lavender

    // 비활성 상태
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(LIGHT_OVERLAY));
    palette.setColor(QPalette::Disabled, QPalette::Text,       QColor(LIGHT_OVERLAY));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(LIGHT_OVERLAY));

    // 플레이스홀더
    palette.setColor(QPalette::PlaceholderText, QColor(LIGHT_SUBTEXT));

    // 미드라인, 섀도우 등
    palette.setColor(QPalette::Mid,     QColor(LIGHT_SURFACE));
    palette.setColor(QPalette::Dark,    QColor(LIGHT_OVERLAY));
    palette.setColor(QPalette::Shadow,  QColor("#9ca0b0"));
    palette.setColor(QPalette::Light,   QColor("#ffffff"));
    palette.setColor(QPalette::Midlight, QColor(LIGHT_BG));

    return palette;
}

// ============================================================
// QSS 스타일시트 생성
// ============================================================

QString ThemeManager::generateStylesheet() const {
    return (resolvedTheme() == ThemeMode::Dark)
        ? buildDarkStylesheet() : buildLightStylesheet();
}

QString ThemeManager::buildDarkStylesheet() const {
    QColor accent = accentColor();
    QString accentHex = accent.name();

    // 다크 테마 QSS — Catppuccin Mocha 기반
    return QStringLiteral(R"(
/* ========================================
 * OrdinalV8 — 다크 테마 (Catppuccin Mocha)
 * ======================================== */

QMainWindow {
    background-color: #1e1e2e;
    color: #cdd6f4;
}

/* 메뉴바 */
QMenuBar {
    background-color: #181825;
    color: #cdd6f4;
    border-bottom: 1px solid #313244;
    padding: 2px;
}

QMenuBar::item:selected {
    background-color: #313244;
    border-radius: 4px;
}

QMenu {
    background-color: #1e1e2e;
    color: #cdd6f4;
    border: 1px solid #313244;
    border-radius: 8px;
    padding: 4px;
}

QMenu::item:selected {
    background-color: )") + accentHex + QStringLiteral(R"(;
    color: #1e1e2e;
    border-radius: 4px;
}

QMenu::separator {
    height: 1px;
    background: #313244;
    margin: 4px 8px;
}

/* 탭바 */
QTabWidget::pane {
    border: 1px solid #313244;
    border-radius: 4px;
    background-color: #1e1e2e;
}

QTabBar::tab {
    background-color: #181825;
    color: #a6adc8;
    padding: 8px 16px;
    border: 1px solid #313244;
    border-bottom: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    margin-right: 2px;
}

QTabBar::tab:selected {
    background-color: #1e1e2e;
    color: #cdd6f4;
    border-bottom: 2px solid )") + accentHex + QStringLiteral(R"(;
}

QTabBar::tab:hover:!selected {
    background-color: #313244;
    color: #cdd6f4;
}

/* 툴바 */
QToolBar {
    background-color: #181825;
    border-bottom: 1px solid #313244;
    spacing: 4px;
    padding: 2px;
}

QToolButton {
    background-color: transparent;
    color: #cdd6f4;
    border: none;
    border-radius: 4px;
    padding: 4px 8px;
}

QToolButton:hover {
    background-color: #313244;
}

QToolButton:pressed {
    background-color: #45475a;
}

/* 주소창 / 입력 필드 */
QLineEdit {
    background-color: #313244;
    color: #cdd6f4;
    border: 1px solid #45475a;
    border-radius: 6px;
    padding: 6px 10px;
    selection-background-color: )") + accentHex + QStringLiteral(R"(;
    selection-color: #1e1e2e;
}

QLineEdit:focus {
    border-color: )") + accentHex + QStringLiteral(R"(;
}

/* 버튼 */
QPushButton {
    background-color: #313244;
    color: #cdd6f4;
    border: 1px solid #45475a;
    border-radius: 6px;
    padding: 6px 16px;
    min-height: 24px;
}

QPushButton:hover {
    background-color: #45475a;
    border-color: )") + accentHex + QStringLiteral(R"(;
}

QPushButton:pressed {
    background-color: )") + accentHex + QStringLiteral(R"(;
    color: #1e1e2e;
}

QPushButton:disabled {
    background-color: #181825;
    color: #45475a;
    border-color: #313244;
}

/* 콤보박스 */
QComboBox {
    background-color: #313244;
    color: #cdd6f4;
    border: 1px solid #45475a;
    border-radius: 6px;
    padding: 4px 8px;
}

QComboBox::drop-down {
    border: none;
    width: 20px;
}

QComboBox QAbstractItemView {
    background-color: #1e1e2e;
    color: #cdd6f4;
    border: 1px solid #313244;
    selection-background-color: )") + accentHex + QStringLiteral(R"(;
    selection-color: #1e1e2e;
}

/* 스핀박스 */
QSpinBox, QDoubleSpinBox {
    background-color: #313244;
    color: #cdd6f4;
    border: 1px solid #45475a;
    border-radius: 6px;
    padding: 4px;
}

/* 체크박스 / 라디오 */
QCheckBox {
    color: #cdd6f4;
    spacing: 8px;
}

QCheckBox::indicator {
    width: 18px;
    height: 18px;
    border: 2px solid #45475a;
    border-radius: 4px;
    background-color: #313244;
}

QCheckBox::indicator:checked {
    background-color: )") + accentHex + QStringLiteral(R"(;
    border-color: )") + accentHex + QStringLiteral(R"(;
}

/* 슬라이더 */
QSlider::groove:horizontal {
    height: 6px;
    background: #313244;
    border-radius: 3px;
}

QSlider::handle:horizontal {
    width: 16px;
    height: 16px;
    margin: -5px 0;
    background: )") + accentHex + QStringLiteral(R"(;
    border-radius: 8px;
}

QSlider::sub-page:horizontal {
    background: )") + accentHex + QStringLiteral(R"(;
    border-radius: 3px;
}

/* 리스트/트리 위젯 */
QListWidget, QTreeWidget, QTableWidget {
    background-color: #181825;
    color: #cdd6f4;
    border: 1px solid #313244;
    border-radius: 4px;
    alternate-background-color: #1e1e2e;
}

QListWidget::item:selected, QTreeWidget::item:selected {
    background-color: )") + accentHex + QStringLiteral(R"(;
    color: #1e1e2e;
}

/* 스크롤바 */
QScrollBar:vertical {
    background: #181825;
    width: 10px;
    border-radius: 5px;
}

QScrollBar::handle:vertical {
    background: #45475a;
    border-radius: 5px;
    min-height: 30px;
}

QScrollBar::handle:vertical:hover {
    background: )") + accentHex + QStringLiteral(R"(;
}

QScrollBar::add-line, QScrollBar::sub-line {
    height: 0;
}

QScrollBar:horizontal {
    background: #181825;
    height: 10px;
    border-radius: 5px;
}

QScrollBar::handle:horizontal {
    background: #45475a;
    border-radius: 5px;
    min-width: 30px;
}

QScrollBar::handle:horizontal:hover {
    background: )") + accentHex + QStringLiteral(R"(;
}

/* 상태바 */
QStatusBar {
    background-color: #181825;
    color: #a6adc8;
    border-top: 1px solid #313244;
}

/* 그룹박스 */
QGroupBox {
    color: #cdd6f4;
    border: 1px solid #313244;
    border-radius: 6px;
    margin-top: 12px;
    padding-top: 16px;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 8px;
    color: )") + accentHex + QStringLiteral(R"(;
}

/* 다이얼로그 배경 */
QDialog {
    background-color: #1e1e2e;
    color: #cdd6f4;
}

/* 텍스트 에디터 */
QTextEdit, QPlainTextEdit, QTextBrowser {
    background-color: #181825;
    color: #cdd6f4;
    border: 1px solid #313244;
    border-radius: 4px;
    selection-background-color: )") + accentHex + QStringLiteral(R"(;
    selection-color: #1e1e2e;
}

/* 프로그레스 바 */
QProgressBar {
    background-color: #313244;
    border-radius: 6px;
    text-align: center;
    color: #cdd6f4;
    height: 12px;
}

QProgressBar::chunk {
    background-color: )") + accentHex + QStringLiteral(R"(;
    border-radius: 6px;
}

/* 라벨 */
QLabel {
    color: #cdd6f4;
}

/* 구분선 */
QFrame[frameShape="4"], QFrame[frameShape="5"] {
    color: #313244;
}
)");
}

QString ThemeManager::buildLightStylesheet() const {
    QColor accent = accentColor();
    QString accentHex = accent.name();

    // 라이트 테마 QSS — Catppuccin Latte 기반
    return QStringLiteral(R"(
/* ========================================
 * OrdinalV8 — 라이트 테마 (Catppuccin Latte)
 * ======================================== */

QMainWindow {
    background-color: #eff1f5;
    color: #4c4f69;
}

QMenuBar {
    background-color: #e6e9ef;
    color: #4c4f69;
    border-bottom: 1px solid #ccd0da;
    padding: 2px;
}

QMenuBar::item:selected {
    background-color: #ccd0da;
    border-radius: 4px;
}

QMenu {
    background-color: #eff1f5;
    color: #4c4f69;
    border: 1px solid #ccd0da;
    border-radius: 8px;
    padding: 4px;
}

QMenu::item:selected {
    background-color: )") + accentHex + QStringLiteral(R"(;
    color: #ffffff;
    border-radius: 4px;
}

QMenu::separator {
    height: 1px;
    background: #ccd0da;
    margin: 4px 8px;
}

QTabWidget::pane {
    border: 1px solid #ccd0da;
    border-radius: 4px;
    background-color: #eff1f5;
}

QTabBar::tab {
    background-color: #e6e9ef;
    color: #6c6f85;
    padding: 8px 16px;
    border: 1px solid #ccd0da;
    border-bottom: none;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
    margin-right: 2px;
}

QTabBar::tab:selected {
    background-color: #eff1f5;
    color: #4c4f69;
    border-bottom: 2px solid )") + accentHex + QStringLiteral(R"(;
}

QTabBar::tab:hover:!selected {
    background-color: #ccd0da;
    color: #4c4f69;
}

QToolBar {
    background-color: #e6e9ef;
    border-bottom: 1px solid #ccd0da;
    spacing: 4px;
    padding: 2px;
}

QToolButton {
    background-color: transparent;
    color: #4c4f69;
    border: none;
    border-radius: 4px;
    padding: 4px 8px;
}

QToolButton:hover {
    background-color: #ccd0da;
}

QToolButton:pressed {
    background-color: #bcc0cc;
}

QLineEdit {
    background-color: #ffffff;
    color: #4c4f69;
    border: 1px solid #ccd0da;
    border-radius: 6px;
    padding: 6px 10px;
    selection-background-color: )") + accentHex + QStringLiteral(R"(;
    selection-color: #ffffff;
}

QLineEdit:focus {
    border-color: )") + accentHex + QStringLiteral(R"(;
}

QPushButton {
    background-color: #e6e9ef;
    color: #4c4f69;
    border: 1px solid #ccd0da;
    border-radius: 6px;
    padding: 6px 16px;
    min-height: 24px;
}

QPushButton:hover {
    background-color: #ccd0da;
    border-color: )") + accentHex + QStringLiteral(R"(;
}

QPushButton:pressed {
    background-color: )") + accentHex + QStringLiteral(R"(;
    color: #ffffff;
}

QPushButton:disabled {
    background-color: #eff1f5;
    color: #bcc0cc;
    border-color: #ccd0da;
}

QComboBox {
    background-color: #ffffff;
    color: #4c4f69;
    border: 1px solid #ccd0da;
    border-radius: 6px;
    padding: 4px 8px;
}

QComboBox::drop-down {
    border: none;
    width: 20px;
}

QComboBox QAbstractItemView {
    background-color: #eff1f5;
    color: #4c4f69;
    border: 1px solid #ccd0da;
    selection-background-color: )") + accentHex + QStringLiteral(R"(;
    selection-color: #ffffff;
}

QSpinBox, QDoubleSpinBox {
    background-color: #ffffff;
    color: #4c4f69;
    border: 1px solid #ccd0da;
    border-radius: 6px;
    padding: 4px;
}

QCheckBox {
    color: #4c4f69;
    spacing: 8px;
}

QCheckBox::indicator {
    width: 18px;
    height: 18px;
    border: 2px solid #ccd0da;
    border-radius: 4px;
    background-color: #ffffff;
}

QCheckBox::indicator:checked {
    background-color: )") + accentHex + QStringLiteral(R"(;
    border-color: )") + accentHex + QStringLiteral(R"(;
}

QSlider::groove:horizontal {
    height: 6px;
    background: #ccd0da;
    border-radius: 3px;
}

QSlider::handle:horizontal {
    width: 16px;
    height: 16px;
    margin: -5px 0;
    background: )") + accentHex + QStringLiteral(R"(;
    border-radius: 8px;
}

QSlider::sub-page:horizontal {
    background: )") + accentHex + QStringLiteral(R"(;
    border-radius: 3px;
}

QListWidget, QTreeWidget, QTableWidget {
    background-color: #ffffff;
    color: #4c4f69;
    border: 1px solid #ccd0da;
    border-radius: 4px;
    alternate-background-color: #eff1f5;
}

QListWidget::item:selected, QTreeWidget::item:selected {
    background-color: )") + accentHex + QStringLiteral(R"(;
    color: #ffffff;
}

QScrollBar:vertical {
    background: #eff1f5;
    width: 10px;
    border-radius: 5px;
}

QScrollBar::handle:vertical {
    background: #bcc0cc;
    border-radius: 5px;
    min-height: 30px;
}

QScrollBar::handle:vertical:hover {
    background: )") + accentHex + QStringLiteral(R"(;
}

QScrollBar::add-line, QScrollBar::sub-line {
    height: 0;
}

QScrollBar:horizontal {
    background: #eff1f5;
    height: 10px;
    border-radius: 5px;
}

QScrollBar::handle:horizontal {
    background: #bcc0cc;
    border-radius: 5px;
    min-width: 30px;
}

QScrollBar::handle:horizontal:hover {
    background: )") + accentHex + QStringLiteral(R"(;
}

QStatusBar {
    background-color: #e6e9ef;
    color: #6c6f85;
    border-top: 1px solid #ccd0da;
}

QGroupBox {
    color: #4c4f69;
    border: 1px solid #ccd0da;
    border-radius: 6px;
    margin-top: 12px;
    padding-top: 16px;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 8px;
    color: )") + accentHex + QStringLiteral(R"(;
}

QDialog {
    background-color: #eff1f5;
    color: #4c4f69;
}

QTextEdit, QPlainTextEdit, QTextBrowser {
    background-color: #ffffff;
    color: #4c4f69;
    border: 1px solid #ccd0da;
    border-radius: 4px;
    selection-background-color: )") + accentHex + QStringLiteral(R"(;
    selection-color: #ffffff;
}

QProgressBar {
    background-color: #ccd0da;
    border-radius: 6px;
    text-align: center;
    color: #4c4f69;
    height: 12px;
}

QProgressBar::chunk {
    background-color: )") + accentHex + QStringLiteral(R"(;
    border-radius: 6px;
}

QLabel {
    color: #4c4f69;
}

QFrame[frameShape="4"], QFrame[frameShape="5"] {
    color: #ccd0da;
}
)");
}

// ============================================================
// QSettings 저장/로드
// ============================================================

void ThemeManager::saveToSettings() const {
    QSettings settings("OrdinalV8", "Theme");

    // 테마 모드 저장
    QString modeStr;
    switch (m_currentTheme) {
        case ThemeMode::Light:  modeStr = "light";  break;
        case ThemeMode::Dark:   modeStr = "dark";   break;
        case ThemeMode::System: modeStr = "system"; break;
    }
    settings.setValue("theme/mode", modeStr);

    // 커스텀 강조색 저장
    if (m_customAccent.isValid()) {
        settings.setValue("theme/accentColor", m_customAccent.name());
    } else {
        settings.remove("theme/accentColor");
    }

    settings.sync();
    qDebug() << "[ThemeManager] 설정 저장됨 — 모드:" << modeStr;
}

void ThemeManager::loadFromSettings() {
    QSettings settings("OrdinalV8", "Theme");

    // 테마 모드 로드
    QString modeStr = settings.value("theme/mode", "system").toString();
    if (modeStr == "light") {
        m_currentTheme = ThemeMode::Light;
    } else if (modeStr == "dark") {
        m_currentTheme = ThemeMode::Dark;
    } else {
        m_currentTheme = ThemeMode::System;
    }

    // 커스텀 강조색 로드
    QString accentStr = settings.value("theme/accentColor").toString();
    if (!accentStr.isEmpty()) {
        QColor color(accentStr);
        if (color.isValid()) {
            m_customAccent = color;
        }
    }

    qDebug() << "[ThemeManager] 설정 로드됨 — 모드:" << modeStr
             << ", 강조색:" << (m_customAccent.isValid() ? m_customAccent.name() : "기본값");
}

} // namespace Ordinal
