# ============================================================
# CPack 패키징 설정 — 크로스 플랫폼 패키지 생성
# ============================================================
# cmake -DCMAKE_BUILD_TYPE=Release .. && make package
#
# 지원 포맷:
#   - Windows: NSIS (.exe 인스톨러)
#   - macOS: DragNDrop (.dmg)
#   - Linux: DEB (.deb), RPM (.rpm), TGZ (.tar.gz)
# ============================================================

# ---- 기본 패키지 정보 ----
set(CPACK_PACKAGE_NAME "ordinalv8")
set(CPACK_PACKAGE_VENDOR "KaztoRay")
set(CPACK_PACKAGE_CONTACT "KaztoRay@users.noreply.github.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "V8 기반 보안 브라우저 + LLM Security Agent")
set(CPACK_PACKAGE_DESCRIPTION
    "OrdinalV8는 V8 JavaScript 엔진을 내장한 보안 중심 웹 브라우저입니다.\n"
    "실시간 피싱 탐지, XSS 방어, LLM 기반 위협 분석 에이전트를 제공합니다.\n"
    "멀티탭 브라우징, 개발자 도구, 추적기 차단을 지원합니다.")

# ---- 버전 정보 ----
file(READ "${CMAKE_SOURCE_DIR}/VERSION" _VERSION_RAW)
string(STRIP "${_VERSION_RAW}" CPACK_PACKAGE_VERSION)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ver_match "${CPACK_PACKAGE_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR ${CMAKE_MATCH_1})
set(CPACK_PACKAGE_VERSION_MINOR ${CMAKE_MATCH_2})
set(CPACK_PACKAGE_VERSION_PATCH ${CMAKE_MATCH_3})

# ---- 공통 설정 ----
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/KaztoRay/ordinalv8")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "OrdinalV8")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_VERBATIM_VARIABLES YES)

# ---- 플랫폼별 생성기 선택 ----
if(WIN32)
    # Windows: NSIS 인스톨러
    set(CPACK_GENERATOR "NSIS")

    set(CPACK_NSIS_DISPLAY_NAME "OrdinalV8 ${CPACK_PACKAGE_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "OrdinalV8")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_URL_INFO_ABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_NSIS_CONTACT "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_NSIS_MODIFY_PATH ON)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "ordinalv8.exe")

    # 바로가기 생성
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$DESKTOP\\\\OrdinalV8.lnk' '$INSTDIR\\\\bin\\\\ordinalv8.exe'
         CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\OrdinalV8.lnk' '$INSTDIR\\\\bin\\\\ordinalv8.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$DESKTOP\\\\OrdinalV8.lnk'
         Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\OrdinalV8.lnk'")

    # 파일 연결
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        WriteRegStr HKCU 'Software\\\\Classes\\\\OrdinalV8.HTML' '' 'HTML Document'
        WriteRegStr HKCU 'Software\\\\Classes\\\\OrdinalV8.HTML\\\\shell\\\\open\\\\command' '' '\\\"$INSTDIR\\\\bin\\\\ordinalv8.exe\\\" \\\"%1\\\"'
    ")

elseif(APPLE)
    # macOS: DragNDrop (.dmg)
    set(CPACK_GENERATOR "DragNDrop")

    set(CPACK_DMG_VOLUME_NAME "OrdinalV8 ${CPACK_PACKAGE_VERSION}")
    set(CPACK_DMG_FORMAT "UDZO")
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "")

    set(CPACK_BUNDLE_NAME "OrdinalV8")
    set(CPACK_BUNDLE_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist")
    set(CPACK_BUNDLE_ICON "")

    set(CPACK_OSX_PACKAGE_VERSION "13.0")

else()
    # Linux: DEB + TGZ
    set(CPACK_GENERATOR "DEB;TGZ")

    # DEB 설정
    set(CPACK_DEBIAN_PACKAGE_NAME "ordinalv8")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "web")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6widgets6 (>= 6.4), libqt6network6 (>= 6.4), libqt6gui6 (>= 6.4), "
        "libqt6core6 (>= 6.4), libcurl4 (>= 7.80), libssl3 (>= 3.0), ca-certificates")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "fonts-noto, fonts-noto-cjk")
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "python3 (>= 3.12), python3-pip")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    # RPM 설정 (옵션)
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Internet")
    set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_RPM_PACKAGE_REQUIRES
        "qt6-qtbase >= 6.4, libcurl >= 7.80, openssl-libs >= 3.0")
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
endif()

# ---- 소스 패키지 설정 ----
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-src")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/"
    "/.git/"
    "/.github/"
    "/node_modules/"
    "/__pycache__/"
    "\\\\.pyc$"
    "\\\\.o$"
    "\\\\.obj$"
    "\\\\.exe$"
    "/\\\\.DS_Store$"
)

# ---- CPack 활성화 ----
include(CPack)
