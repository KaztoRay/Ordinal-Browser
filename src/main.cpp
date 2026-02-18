/**
 * @file main.cpp
 * @brief Ordinal Browser 메인 진입점
 *
 * Qt WebEngine (Chromium) 기반 보안 웹 브라우저
 * 광고 차단, WebRTC 보호, 핑거프린팅 방지, LLM 보안 에이전트 내장
 */

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QSettings>
#include <QDir>
#include <QStandardPaths>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include <csignal>
#include <iostream>

#include "engine/browser_window.h"
#include "engine/theme_engine.h"

namespace {

QApplication* g_app = nullptr;

void signalHandler(int signum) {
    std::cerr << "\n[Ordinal] 시그널 " << signum << " 수신 — 종료 중..." << std::endl;
    if (g_app) g_app->quit();
}

void installSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    // Qt WebEngine 초기화 (QApplication 생성 전에 호출)
    QtWebEngineQuick::initialize();

    QApplication app(argc, argv);
    QApplication::setApplicationName("Ordinal Browser");
    QApplication::setApplicationVersion("1.2.0");
    QApplication::setOrganizationName("Ordinal");
    QApplication::setOrganizationDomain("ordinal.dev");
    QApplication::setWindowIcon(QIcon(":/icons/ordinal-browser.png"));

    g_app = &app;

    // CLI 인수 파싱
    QCommandLineParser parser;
    parser.setApplicationDescription("Ordinal Browser — AI 기반 보안 웹 브라우저");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption incognitoOption("incognito", "시크릿 모드");
    parser.addOption(incognitoOption);

    parser.addPositionalArgument("url", "시작 URL", "[url]");
    parser.process(app);

    installSignalHandlers();

    // 테마 적용 (시스템 기본)
    QSettings themeSettings("Ordinal", "OrdinalBrowser");
    int themeIdx = themeSettings.value("appearance/theme", 0).toInt();
    Ordinal::Engine::ThemeEngine::apply(
        static_cast<Ordinal::Engine::ThemeEngine::Theme>(themeIdx), &app);

    // 메인 윈도우
    Ordinal::Engine::BrowserWindow window;
    window.resize(1280, 800);
    window.show();

    // CLI에서 URL 전달된 경우
    QStringList positionalArgs = parser.positionalArguments();
    if (!positionalArgs.isEmpty()) {
        window.navigateTo(positionalArgs.first());
    }

    std::cout << "[Ordinal] Ordinal Browser v1.2.0 시작 완료" << std::endl;
    std::cout << "[Ordinal] Chromium (Qt WebEngine) 기반" << std::endl;
    std::cout << "[Ordinal] 광고 차단 / WebRTC 보호 / 핑거프린팅 방지 활성화" << std::endl;

    return app.exec();
}
