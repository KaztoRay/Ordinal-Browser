/**
 * @file main.cpp
 * @brief Ordinal Browser 메인 진입점
 *
 * QApplication 초기화, V8 엔진 설정, gRPC 채널 연결,
 * CLI 인수 파싱, 시그널 핸들링을 수행합니다.
 *
 * CLI 옵션:
 *   --port <포트>       gRPC 에이전트 포트 (기본: 50051)
 *   --no-agent          보안 에이전트 연결 비활성화
 *   --dev-tools          DevTools 패널 기본 표시
 *   --profile-dir <경로> 사용자 프로필 디렉토리
 */

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QStandardPaths>

#include <csignal>
#include <iostream>
#include <memory>
#include <string>

// V8 엔진 (조건부 포함)
#include <v8.h>
#include <libplatform/libplatform.h>

// gRPC (조건부 포함)
#ifdef HAS_GRPC
#include <grpcpp/grpcpp.h>
#endif

#include "core/v8_engine.h"
#include "ui/main_window.h"

namespace {

// ============================================================
// 전역 상태 (시그널 핸들러에서 접근)
// ============================================================

/// 메인 윈도우 포인터 (안전 종료용)
QApplication* g_app = nullptr;

/**
 * @brief UNIX 시그널 핸들러
 *
 * SIGINT(Ctrl+C), SIGTERM 수신 시 Qt 이벤트 루프를 종료합니다.
 */
void signalHandler(int signum) {
    std::cerr << "\n[Ordinal] 시그널 " << signum << " 수신 — 종료 중..." << std::endl;
    if (g_app) {
        g_app->quit();
    }
}

/**
 * @brief 시그널 핸들러 설치
 */
void installSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#ifndef _WIN32
    // SIGPIPE 무시 (소켓 에러 방지)
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

// ============================================================
// V8 초기화/종료
// ============================================================

/// V8 플랫폼 (전역 유일)
static std::unique_ptr<v8::Platform> g_v8_platform;

/**
 * @brief V8 엔진 전역 초기화
 *
 * ICU 데이터, 플랫폼, V8 자체를 초기화합니다.
 * macOS ARM64 환경에 최적화됩니다.
 *
 * @param icu_data_path ICU 데이터 파일 경로 (빈 문자열이면 기본 경로)
 * @return 초기화 성공 여부
 */
bool initializeV8([[maybe_unused]] const std::string& icu_data_path = "") {
    // ICU 데이터 초기화 (국제화 지원)
    if (!icu_data_path.empty()) {
        v8::V8::InitializeICUDefaultLocation(icu_data_path.c_str());
    } else {
        v8::V8::InitializeICUDefaultLocation(nullptr);
    }

    // V8 외부 스타트업 데이터 (스냅샷)
    v8::V8::InitializeExternalStartupData(nullptr);

    // 플랫폼 초기화 (스레드 풀 포함)
    g_v8_platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(g_v8_platform.get());

    // V8 엔진 초기화
    v8::V8::Initialize();

    std::cout << "[Ordinal] V8 엔진 초기화 완료" << std::endl;
    return true;
}

/**
 * @brief V8 엔진 전역 종료
 *
 * V8 리소스를 해제하고 플랫폼을 종료합니다.
 */
void shutdownV8() {
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    g_v8_platform.reset();
    std::cout << "[Ordinal] V8 엔진 종료 완료" << std::endl;
}

// ============================================================
// gRPC 채널 연결
// ============================================================

#ifdef HAS_GRPC
/// gRPC 에이전트 채널
static std::shared_ptr<grpc::Channel> g_grpc_channel;
#endif

/**
 * @brief gRPC 에이전트 채널 생성
 *
 * Python 보안 에이전트의 gRPC 서버에 연결합니다.
 *
 * @param host gRPC 서버 호스트
 * @param port gRPC 서버 포트
 * @return 연결 성공 여부
 */
bool connectToAgent(
    [[maybe_unused]] const std::string& host,
    [[maybe_unused]] int port
) {
#ifdef HAS_GRPC
    std::string target = host + ":" + std::to_string(port);
    std::cout << "[Ordinal] 보안 에이전트 연결 시도: " << target << std::endl;

    // 채널 옵션 설정
    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(10 * 1024 * 1024);  // 10MB
    args.SetMaxSendMessageSize(10 * 1024 * 1024);

    // Insecure 채널 (로컬 IPC이므로 TLS 불필요)
    g_grpc_channel = grpc::CreateCustomChannel(
        target,
        grpc::InsecureChannelCredentials(),
        args
    );

    if (!g_grpc_channel) {
        std::cerr << "[Ordinal] gRPC 채널 생성 실패" << std::endl;
        return false;
    }

    // 연결 확인 (5초 대기)
    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
    bool connected = g_grpc_channel->WaitForConnected(deadline);

    if (connected) {
        std::cout << "[Ordinal] 보안 에이전트 연결 성공: " << target << std::endl;
    } else {
        std::cerr << "[Ordinal] 보안 에이전트 연결 타임아웃 — 독립 모드로 실행" << std::endl;
    }

    return true;
#else
    std::cout << "[Ordinal] gRPC 미포함 빌드 — 에이전트 연결 비활성화" << std::endl;
    return true;
#endif
}

/**
 * @brief gRPC 리소스 종료
 */
void shutdownGrpc() {
#ifdef HAS_GRPC
    g_grpc_channel.reset();
    std::cout << "[Ordinal] gRPC 채널 종료 완료" << std::endl;
#endif
}

// ============================================================
// 프로필 디렉토리 설정
// ============================================================

/**
 * @brief 사용자 프로필 디렉토리 생성/확인
 *
 * @param profile_dir 프로필 경로 (빈 문자열이면 기본 경로 사용)
 * @return 실제 프로필 디렉토리 경로
 */
QString ensureProfileDir(const QString& profile_dir) {
    QString path = profile_dir;

    if (path.isEmpty()) {
        // 기본 프로필 경로: ~/Library/Application Support/OrdinalBrowser/
        path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (path.isEmpty()) {
            path = QDir::homePath() + "/.ordinal-browser";
        }
    }

    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
        std::cout << "[Ordinal] 프로필 디렉토리 생성: "
                  << path.toStdString() << std::endl;
    }

    return path;
}

} // anonymous namespace


// ============================================================
// 메인 함수
// ============================================================

int main(int argc, char* argv[]) {
    // ---- Qt 애플리케이션 초기화 ----
    QApplication app(argc, argv);
    QApplication::setApplicationName("Ordinal Browser");
    QApplication::setApplicationVersion("1.2.0");
    QApplication::setOrganizationName("Ordinal");
    QApplication::setOrganizationDomain("ordinal.dev");

    g_app = &app;

    // ---- CLI 인수 파싱 ----
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Ordinal Browser — AI 기반 보안 웹 브라우저"
    );
    parser.addHelpOption();
    parser.addVersionOption();

    // --port: gRPC 에이전트 포트
    QCommandLineOption portOption(
        "port",
        "보안 에이전트 gRPC 포트 (기본: 50051)",
        "port",
        "50051"
    );
    parser.addOption(portOption);

    // --no-agent: 에이전트 연결 비활성화
    QCommandLineOption noAgentOption(
        "no-agent",
        "보안 에이전트 연결 비활성화 (독립 모드)"
    );
    parser.addOption(noAgentOption);

    // --dev-tools: DevTools 기본 표시
    QCommandLineOption devToolsOption(
        "dev-tools",
        "개발자 도구 패널 기본 표시"
    );
    parser.addOption(devToolsOption);

    // --profile-dir: 사용자 프로필 디렉토리
    QCommandLineOption profileOption(
        "profile-dir",
        "사용자 프로필 디렉토리 경로",
        "directory",
        ""
    );
    parser.addOption(profileOption);

    parser.process(app);

    // CLI 값 추출
    int grpc_port = parser.value(portOption).toInt();
    bool no_agent = parser.isSet(noAgentOption);
    bool show_dev_tools = parser.isSet(devToolsOption);
    QString profile_dir = parser.value(profileOption);

    // ---- 시그널 핸들러 설치 ----
    installSignalHandlers();

    // ---- 프로필 디렉토리 ----
    QString resolved_profile = ensureProfileDir(profile_dir);
    std::cout << "[Ordinal] 프로필 디렉토리: "
              << resolved_profile.toStdString() << std::endl;

    // ---- V8 엔진 초기화 ----
    if (!initializeV8()) {
        std::cerr << "[Ordinal] V8 초기화 실패 — 종료" << std::endl;
        return 1;
    }

    // ---- gRPC 에이전트 연결 ----
    if (!no_agent) {
        if (!connectToAgent("localhost", grpc_port)) {
            std::cerr << "[Ordinal] 에이전트 연결 실패 — 독립 모드로 계속" << std::endl;
        }
    } else {
        std::cout << "[Ordinal] 에이전트 연결 비활성화 (--no-agent)" << std::endl;
    }

    // ---- 메인 윈도우 생성 및 표시 ----
    std::cout << "[Ordinal] 메인 윈도우 생성 중..." << std::endl;

    ordinal::ui::MainWindow mainWindow;
    mainWindow.setWindowTitle("Ordinal Browser");
    mainWindow.resize(1280, 800);

    // DevTools 옵션 적용
    if (show_dev_tools) {
        mainWindow.toggleDevTools();
    }

    mainWindow.show();

    std::cout << "[Ordinal] Ordinal Browser v1.2.0 시작 완료" << std::endl;
    std::cout << "[Ordinal] gRPC 포트: " << grpc_port
              << ", 에이전트: " << (no_agent ? "비활성화" : "활성화")
              << ", DevTools: " << (show_dev_tools ? "표시" : "숨김")
              << std::endl;

    // ---- Qt 이벤트 루프 실행 ----
    int exit_code = app.exec();

    // ---- 정리 ----
    std::cout << "[Ordinal] 종료 중..." << std::endl;

    shutdownGrpc();
    shutdownV8();

    std::cout << "[Ordinal] 정상 종료 (코드: " << exit_code << ")" << std::endl;
    return exit_code;
}
