#pragma once

#include <QApplication>
#include <QPalette>
#include <QString>
#include <QWidget>

namespace Ordinal {
namespace Engine {

// ============================================================
// ThemeEngine — 다크모드/테마 관리
// ============================================================
class ThemeEngine {
public:
    enum Theme {
        SystemDefault = 0,
        Light = 1,
        Dark = 2
    };

    static void apply(Theme theme, QApplication* app = nullptr);
    static void applyDark(QApplication* app = nullptr);
    static void applyLight(QApplication* app = nullptr);
    static void applySystem(QApplication* app = nullptr);

    // CSS 스타일시트 생성
    static QString darkStyleSheet();
    static QString lightStyleSheet();

    // 시스템 다크모드 감지 (macOS)
    static bool isSystemDarkMode();
};

} // namespace Engine
} // namespace Ordinal
