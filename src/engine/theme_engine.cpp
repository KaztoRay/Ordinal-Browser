#include "theme_engine.h"

#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace Ordinal {
namespace Engine {

void ThemeEngine::apply(Theme theme, QApplication* app)
{
    switch (theme) {
    case Dark: applyDark(app); break;
    case Light: applyLight(app); break;
    case SystemDefault:
    default: applySystem(app); break;
    }
}

void ThemeEngine::applyDark(QApplication* app)
{
    if (!app) app = qobject_cast<QApplication*>(QApplication::instance());
    if (!app) return;

    QPalette darkPalette;
    QColor darkBg(30, 30, 30);
    QColor darkWidget(45, 45, 45);
    QColor darkText(220, 220, 220);
    QColor highlight(66, 133, 244);    // Google Blue
    QColor disabled(100, 100, 100);
    QColor link(130, 177, 255);

    darkPalette.setColor(QPalette::Window, darkBg);
    darkPalette.setColor(QPalette::WindowText, darkText);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, darkWidget);
    darkPalette.setColor(QPalette::ToolTipBase, QColor(50, 50, 50));
    darkPalette.setColor(QPalette::ToolTipText, darkText);
    darkPalette.setColor(QPalette::Text, darkText);
    darkPalette.setColor(QPalette::Button, darkWidget);
    darkPalette.setColor(QPalette::ButtonText, darkText);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, link);
    darkPalette.setColor(QPalette::Highlight, highlight);
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);

    darkPalette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    darkPalette.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);

    app->setPalette(darkPalette);
    app->setStyleSheet(darkStyleSheet());
}

void ThemeEngine::applyLight(QApplication* app)
{
    if (!app) app = qobject_cast<QApplication*>(QApplication::instance());
    if (!app) return;

    app->setPalette(QApplication::style()->standardPalette());
    app->setStyleSheet(lightStyleSheet());
}

void ThemeEngine::applySystem(QApplication* app)
{
    if (isSystemDarkMode()) {
        applyDark(app);
    } else {
        applyLight(app);
    }
}

QString ThemeEngine::darkStyleSheet()
{
    return R"(
        QMainWindow {
            background-color: #1e1e1e;
        }
        QToolBar {
            background-color: #2d2d2d;
            border-bottom: 1px solid #3d3d3d;
            spacing: 4px;
            padding: 2px;
        }
        QMenuBar {
            background-color: #2d2d2d;
            color: #dcdcdc;
            border-bottom: 1px solid #3d3d3d;
        }
        QMenuBar::item:selected {
            background-color: #4285f4;
            color: white;
        }
        QMenu {
            background-color: #2d2d2d;
            color: #dcdcdc;
            border: 1px solid #3d3d3d;
        }
        QMenu::item:selected {
            background-color: #4285f4;
        }
        QTabWidget::pane {
            border: none;
        }
        QTabBar::tab {
            background: #2d2d2d;
            color: #aaa;
            border: none;
            padding: 6px 16px;
            min-width: 100px;
            max-width: 200px;
        }
        QTabBar::tab:selected {
            background: #1e1e1e;
            color: white;
            border-bottom: 2px solid #4285f4;
        }
        QTabBar::tab:hover {
            background: #383838;
            color: #ddd;
        }
        QLineEdit {
            background: #1a1a1a;
            color: #87CEEB;
            border: 1px solid #3d3d3d;
            border-radius: 15px;
            padding: 4px 14px;
            selection-background-color: #4285f4;
        }
        QLineEdit:focus {
            border-color: #4285f4;
            background: #252525;
            color: #87CEEB;
        }
        QStatusBar {
            background: #2d2d2d;
            color: #888;
            border-top: 1px solid #3d3d3d;
        }
        QProgressBar {
            border: none;
            background: transparent;
        }
        QProgressBar::chunk {
            background: #4285f4;
        }
        QScrollBar:vertical {
            background: #1e1e1e;
            width: 8px;
        }
        QScrollBar::handle:vertical {
            background: #555;
            border-radius: 4px;
            min-height: 20px;
        }
        QScrollBar:horizontal {
            background: #1e1e1e;
            height: 8px;
        }
        QScrollBar::handle:horizontal {
            background: #555;
            border-radius: 4px;
        }
        QGroupBox {
            color: #dcdcdc;
            border: 1px solid #3d3d3d;
            border-radius: 4px;
            margin-top: 8px;
            padding-top: 16px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
        }
        QPushButton {
            background: #3d3d3d;
            color: #dcdcdc;
            border: 1px solid #555;
            border-radius: 4px;
            padding: 6px 16px;
        }
        QPushButton:hover {
            background: #4d4d4d;
        }
        QPushButton:pressed {
            background: #555;
        }
        QComboBox {
            background: #2d2d2d;
            color: #dcdcdc;
            border: 1px solid #3d3d3d;
            border-radius: 4px;
            padding: 4px 8px;
        }
        QSpinBox {
            background: #2d2d2d;
            color: #dcdcdc;
            border: 1px solid #3d3d3d;
            border-radius: 4px;
        }
        QCheckBox {
            color: #dcdcdc;
        }
        QDialog {
            background: #1e1e1e;
        }
        QListWidget {
            background: #1a1a1a;
            color: #dcdcdc;
            border: 1px solid #3d3d3d;
        }
        QListWidget::item:selected {
            background: #4285f4;
        }
        QToolTip {
            background: #3d3d3d;
            color: #dcdcdc;
            border: 1px solid #555;
            padding: 4px;
        }
    )";
}

QString ThemeEngine::lightStyleSheet()
{
    return R"(
        QMainWindow {
            background-color: #f8f8f8;
        }
        QMenuBar {
            background-color: #f0f0f0;
            color: #333;
            border-bottom: 1px solid #ddd;
        }
        QMenuBar::item:selected {
            background-color: #4285f4;
            color: white;
        }
        QMenu {
            background-color: white;
            color: #333;
            border: 1px solid #ddd;
        }
        QMenu::item:selected {
            background-color: #4285f4;
            color: white;
        }
        QLineEdit {
            background: #fff;
            color: #87CEEB;
            border: 1px solid #ccc;
            border-radius: 15px;
            padding: 4px 14px;
            font-size: 13px;
            selection-background-color: #4285f4;
            selection-color: white;
        }
        QLineEdit:focus {
            border-color: #4285f4;
            background: #fff;
        }
        QStatusBar {
            background: #f0f0f0;
            color: #666;
            border-top: 1px solid #ddd;
        }
        QTabBar::tab {
            background: #f0f0f0;
            color: #333;
            border: none;
            padding: 6px 16px;
            min-width: 100px;
            max-width: 200px;
        }
        QTabBar::tab:selected {
            background: white;
            color: black;
            border-bottom: 2px solid #4285f4;
        }
        QTabBar::tab:hover {
            background: #e0e0e0;
        }
        QToolBar {
            background: #f5f5f5;
            border-bottom: 1px solid #ddd;
            spacing: 4px;
            padding: 2px;
        }
        QProgressBar {
            border: none;
            background: transparent;
        }
        QProgressBar::chunk {
            background: #4285f4;
        }
    )";
}

bool ThemeEngine::isSystemDarkMode()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    auto scheme = QApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark;
#else
    QPalette palette = QApplication::palette();
    return palette.window().color().lightness() < 128;
#endif
}

} // namespace Engine
} // namespace Ordinal
