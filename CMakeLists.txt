cmake_minimum_required(VERSION 3.24)
project(OrdinalBrowser VERSION 1.2.0 LANGUAGES CXX)

# C++20 표준 설정
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 빌드 타입 기본값
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# 컴파일러 경고 옵션
add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)

# macOS 특화 설정
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS 아키텍처")
    set(CMAKE_MACOSX_RPATH ON)
endif()

# ============================================================
# 의존성 찾기
# ============================================================

# Qt6 - UI 프레임워크
find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Network)
qt_standard_project_setup()

# OpenSSL - SSL/TLS 인증서 검증
find_package(OpenSSL REQUIRED)

# CURL - HTTP 클라이언트
find_package(CURL REQUIRED)

# gRPC & Protobuf - C++ ↔ Python IPC
find_package(protobuf CONFIG QUIET)
find_package(gRPC CONFIG QUIET)

# V8 엔진 경로 (사용자 설정 또는 기본값)
set(V8_ROOT "${CMAKE_SOURCE_DIR}/third_party/v8" CACHE PATH "V8 엔진 루트 디렉토리")
set(V8_INCLUDE_DIR "${V8_ROOT}/include" CACHE PATH "V8 헤더 디렉토리")
set(V8_LIB_DIR "${V8_ROOT}/out.gn/arm64.release/obj" CACHE PATH "V8 라이브러리 디렉토리")

# V8 스텁 헤더는 항상 포함 (third_party/v8/include에 스텁 제공)
# 실제 V8 라이브러리는 존재할 때만 링크

# ============================================================
# 소스 파일 수집
# ============================================================

# 코어 모듈
set(CORE_SOURCES
    src/core/v8_engine.cpp
    src/core/browser_app.cpp
    src/core/tab.cpp
    src/core/page.cpp
    src/core/js_bridge.cpp
    src/core/tab_manager.cpp
    src/core/performance_monitor.cpp
    src/core/media_controller.cpp
)

# 네트워크 모듈
set(NETWORK_SOURCES
    src/network/http_client.cpp
    src/network/request_interceptor.cpp
    src/network/ssl_inspector.cpp
    src/network/websocket_client.cpp
    src/network/dns_resolver.cpp
    src/network/proxy_manager.cpp
    src/network/download_accelerator.cpp
)

# 보안 모듈
set(SECURITY_SOURCES
    src/security/security_agent.cpp
    src/security/phishing_detector.cpp
    src/security/xss_analyzer.cpp
    src/security/script_analyzer.cpp
    src/security/privacy_tracker.cpp
    src/security/cert_validator.cpp
)

# 렌더링 모듈
set(RENDERING_SOURCES
    src/rendering/html_parser.cpp
    src/rendering/css_parser.cpp
    src/rendering/dom_tree.cpp
    src/rendering/layout_engine.cpp
)

# UI 모듈
set(UI_SOURCES
    src/ui/main_window.cpp
    src/ui/tab_bar.cpp
    src/ui/address_bar.cpp
    src/ui/security_panel.cpp
    src/ui/dev_tools_panel.cpp
    src/ui/about_dialog.cpp
    src/ui/settings_dialog.cpp
    src/ui/theme_manager.cpp
    src/ui/reader_mode.cpp
)

# 데이터 모듈
set(DATA_SOURCES
    src/data/bookmark_manager.cpp
    src/data/data_store.cpp
    src/data/download_manager.cpp
    src/data/history_manager.cpp
    src/data/session_manager.cpp
)

# 확장 프로그램 모듈
set(EXTENSION_SOURCES
    src/extensions/extension.cpp
    src/extensions/extension_manager.cpp
    src/extensions/extension_api.cpp
    src/extensions/content_script.cpp
)

# 광고 차단 모듈
set(ADBLOCK_SOURCES
    src/adblock/adblock_engine.cpp
    src/adblock/filter_parser.cpp
    src/adblock/cosmetic_filter.cpp
)

# 프라이버시 모듈
set(PRIVACY_SOURCES
    src/privacy/password_manager.cpp
    src/privacy/cookie_manager.cpp
    src/privacy/autofill_engine.cpp
    src/privacy/consent_handler.cpp
    src/privacy/webrtc_guard.cpp
)

# ============================================================
# 실행 파일 빌드
# ============================================================

qt_add_executable(ordinal-browser
    src/main.cpp
    ${CORE_SOURCES}
    ${NETWORK_SOURCES}
    ${SECURITY_SOURCES}
    ${RENDERING_SOURCES}
    ${UI_SOURCES}
    ${DATA_SOURCES}
    ${EXTENSION_SOURCES}
    ${ADBLOCK_SOURCES}
    ${PRIVACY_SOURCES}
)

# 헤더 포함 경로
target_include_directories(ordinal-browser PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${V8_INCLUDE_DIR}
)

# 라이브러리 링크
target_link_libraries(ordinal-browser PRIVATE
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
)

# V8 라이브러리 (옵션 — 없으면 스텁 사용)
find_library(V8_LIB v8_monolith PATHS ${V8_LIB_DIR} NO_DEFAULT_PATH)

if(V8_LIB)
    target_link_directories(ordinal-browser PRIVATE ${V8_LIB_DIR})
    target_link_libraries(ordinal-browser PRIVATE v8_monolith)
    target_compile_definitions(ordinal-browser PRIVATE V8_COMPRESS_POINTERS V8_ENABLE_SANDBOX)
    message(STATUS "V8 라이브러리 발견: ${V8_LIB}")
else()
    message(WARNING "V8 라이브러리 없음 — 스텁 모드로 빌드")
    target_compile_definitions(ordinal-browser PRIVATE V8_STUB_MODE=1)
endif()

# gRPC 링크 (존재하는 경우에만)
if(gRPC_FOUND)
    target_link_libraries(ordinal-browser PRIVATE gRPC::grpc++ protobuf::libprotobuf)
    target_compile_definitions(ordinal-browser PRIVATE HAS_GRPC)
endif()

# ============================================================
# 설치 규칙
# ============================================================

install(TARGETS ordinal-browser
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# macOS 번들 설정
if(APPLE)
    set_target_properties(ordinal-browser PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.ordinal.browser"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    )
endif()

# ============================================================
# 테스트 (추후 확장)
# ============================================================
enable_testing()

message(STATUS "======================================")
message(STATUS "Ordinal Browser v${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "V8 Root: ${V8_ROOT}")
message(STATUS "======================================")
