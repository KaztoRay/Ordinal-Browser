#!/bin/bash

# ============================================================
# Linux AppImage 빌드 스크립트
# ============================================================
# Ordinal Browser를 이식 가능한 AppImage로 패키징합니다.
# 모든 의존성(Qt, V8, 라이브러리)을 포함하여 단일 실행 파일을 생성합니다.
#
# 사용법:
#   ./packaging/linux/create_appimage.sh [빌드_디렉토리]
#
# 필요한 도구:
#   - linuxdeployqt 또는 linuxdeploy (자동 다운로드)
#   - patchelf (RPATH 수정)
#   - file, strip
# ============================================================

set -euo pipefail

# ---- 설정 변수 ----
APP_NAME="Ordinal Browser"
BINARY_NAME="ordinal-browser"
VERSION=$(cat "$(dirname "$0")/../../VERSION" 2>/dev/null || echo "1.0.0")
ARCH=$(uname -m)
BUILD_DIR="${1:-$(dirname "$0")/../../build}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

APPIMAGE_NAME="OrdinalBrowser-${VERSION}-${ARCH}.AppImage"
APPDIR="${BUILD_DIR}/AppDir"

echo "🐧 Ordinal Browser AppImage 빌드 시작 (v${VERSION}, ${ARCH})"
echo "============================================================"

# ---- 빌드 바이너리 확인 ----
if [ ! -f "${BUILD_DIR}/${BINARY_NAME}" ]; then
    echo "❌ 바이너리를 찾을 수 없습니다: ${BUILD_DIR}/${BINARY_NAME}"
    echo "   먼저 빌드를 실행하세요: mkdir build && cd build && cmake .. && make -j\$(nproc)"
    exit 1
fi

# ---- 기존 AppDir 정리 ----
rm -rf "${APPDIR}"

# ---- AppDir 디렉토리 구조 생성 ----
echo "📂 AppDir 구조 생성 중..."
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/lib"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"
mkdir -p "${APPDIR}/usr/share/metainfo"
mkdir -p "${APPDIR}/usr/plugins"

# ---- 바이너리 복사 ----
echo "🔧 바이너리 복사 중..."
cp "${BUILD_DIR}/${BINARY_NAME}" "${APPDIR}/usr/bin/${BINARY_NAME}"
chmod +x "${APPDIR}/usr/bin/${BINARY_NAME}"

# 디버그 심볼 제거 (크기 최적화)
if command -v strip &> /dev/null; then
    strip --strip-unneeded "${APPDIR}/usr/bin/${BINARY_NAME}" 2>/dev/null || true
    echo "   ✅ 디버그 심볼 제거 완료"
fi

# ---- 공유 라이브러리 복사 ----
echo "📚 공유 라이브러리 복사 중..."

# ldd로 의존성 추출 후 복사
ldd "${APPDIR}/usr/bin/${BINARY_NAME}" 2>/dev/null | \
    grep "=> /" | \
    awk '{print $3}' | \
    while read -r lib; do
        # 시스템 기본 라이브러리는 제외 (libc, libm, libpthread 등)
        BASENAME=$(basename "$lib")
        case "$BASENAME" in
            libc.so*|libm.so*|libpthread.so*|libdl.so*|librt.so*|\
            libgcc_s.so*|libstdc++.so*|ld-linux*.so*)
                continue
                ;;
        esac
        cp -n "$lib" "${APPDIR}/usr/lib/" 2>/dev/null || true
    done

echo "   라이브러리 수: $(ls -1 "${APPDIR}/usr/lib/" 2>/dev/null | wc -l)"

# ---- V8 라이브러리 복사 ----
V8_LIB_DIR="${PROJECT_ROOT}/third_party/v8/lib"
if [ -d "${V8_LIB_DIR}" ]; then
    echo "📚 V8 라이브러리 복사 중..."
    find "${V8_LIB_DIR}" -name "*.so*" -exec cp {} "${APPDIR}/usr/lib/" \;
fi

# ---- Qt 플러그인 복사 ----
echo "🔌 Qt 플러그인 복사 중..."
QT_PLUGIN_PATH=""

# Qt 플러그인 경로 탐색
for qt_path in \
    "/usr/lib/${ARCH}-linux-gnu/qt6/plugins" \
    "/usr/lib/qt6/plugins" \
    "/usr/lib64/qt6/plugins" \
    "$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || true)"; do
    if [ -d "$qt_path" ]; then
        QT_PLUGIN_PATH="$qt_path"
        break
    fi
done

if [ -n "${QT_PLUGIN_PATH}" ]; then
    # 필수 플러그인 복사
    for plugin_dir in platforms platformthemes imageformats iconengines xcbglintegrations wayland-shell-integration; do
        if [ -d "${QT_PLUGIN_PATH}/${plugin_dir}" ]; then
            mkdir -p "${APPDIR}/usr/plugins/${plugin_dir}"
            cp -r "${QT_PLUGIN_PATH}/${plugin_dir}/"*.so "${APPDIR}/usr/plugins/${plugin_dir}/" 2>/dev/null || true
        fi
    done
    echo "   ✅ Qt 플러그인 복사 완료"
else
    echo "   ⚠️  Qt 플러그인 경로를 찾을 수 없습니다."
fi

# ---- 데스크톱 파일 복사 ----
echo "🖥️  데스크톱 엔트리 설정 중..."
if [ -f "${SCRIPT_DIR}/ordinal-browser.desktop" ]; then
    cp "${SCRIPT_DIR}/ordinal-browser.desktop" "${APPDIR}/usr/share/applications/${BINARY_NAME}.desktop"
    # AppDir 루트에도 복사 (AppImage 요구사항)
    cp "${SCRIPT_DIR}/ordinal-browser.desktop" "${APPDIR}/${BINARY_NAME}.desktop"
else
    cat > "${APPDIR}/${BINARY_NAME}.desktop" << DESKTOP
[Desktop Entry]
Name=Ordinal Browser
Comment=V8-based Security Browser with LLM Agent
Exec=ordinal-browser %u
Icon=ordinal-browser
Type=Application
Categories=Network;WebBrowser;Security;
MimeType=text/html;x-scheme-handler/http;x-scheme-handler/https;
StartupNotify=true
DESKTOP
    cp "${APPDIR}/${BINARY_NAME}.desktop" "${APPDIR}/usr/share/applications/"
fi

# ---- 아이콘 설정 (기본 아이콘 생성) ----
echo "🎨 아이콘 설정 중..."
# 아이콘 파일이 없으면 빈 SVG 생성 (CI에서 실제 아이콘으로 교체)
if [ ! -f "${APPDIR}/usr/share/icons/hicolor/256x256/apps/${BINARY_NAME}.png" ]; then
    # 간단한 SVG 아이콘 생성
    cat > "${APPDIR}/usr/share/icons/hicolor/scalable/apps/${BINARY_NAME}.svg" << 'SVG'
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <defs>
    <linearGradient id="bg" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#1a73e8"/>
      <stop offset="100%" style="stop-color:#0d47a1"/>
    </linearGradient>
  </defs>
  <rect width="256" height="256" rx="48" fill="url(#bg)"/>
  <text x="128" y="160" text-anchor="middle" font-family="Arial,sans-serif"
        font-size="120" font-weight="bold" fill="white">O</text>
  <circle cx="200" cy="56" r="28" fill="#4caf50" stroke="white" stroke-width="4"/>
  <path d="M190 56 L197 63 L212 48" stroke="white" stroke-width="5"
        fill="none" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
SVG
    # AppDir 루트 심볼릭 링크 (AppImage 요구사항)
    ln -sf usr/share/icons/hicolor/scalable/apps/${BINARY_NAME}.svg \
        "${APPDIR}/${BINARY_NAME}.svg"
fi

# ---- AppRun 스크립트 생성 ----
echo "🚀 AppRun 스크립트 생성 중..."
cat > "${APPDIR}/AppRun" << 'APPRUN'
#!/bin/bash
# AppImage 실행 스크립트
# AppDir 내부 경로를 기준으로 라이브러리 및 플러그인을 로드합니다.

SELF=$(readlink -f "$0")
HERE=${SELF%/*}

# 라이브러리 경로 설정
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH:-}"

# Qt 플러그인 경로 설정
export QT_PLUGIN_PATH="${HERE}/usr/plugins:${QT_PLUGIN_PATH:-}"

# Qt 플랫폼 테마 (시스템 통합)
export QT_QPA_PLATFORMTHEME="${QT_QPA_PLATFORMTHEME:-gtk3}"

# XDG 데이터 경로
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

# 실행
exec "${HERE}/usr/bin/ordinal-browser" "$@"
APPRUN
chmod +x "${APPDIR}/AppRun"

# ---- AppStream 메타데이터 ----
echo "📝 AppStream 메타데이터 생성 중..."
cat > "${APPDIR}/usr/share/metainfo/${BINARY_NAME}.appdata.xml" << APPDATA
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>com.kaztoray.ordinal-browser</id>
  <name>Ordinal Browser</name>
  <summary>V8 기반 보안 브라우저 + LLM Security Agent</summary>
  <metadata_license>MIT</metadata_license>
  <project_license>MIT</project_license>
  <description>
    <p>Ordinal Browser는 V8 JavaScript 엔진 기반의 보안 중심 웹 브라우저입니다.</p>
    <p>실시간 피싱 탐지, XSS 방어, LLM 기반 위협 분석을 제공합니다.</p>
  </description>
  <url type="homepage">https://github.com/KaztoRay/ordinal-browser</url>
  <url type="bugtracker">https://github.com/KaztoRay/ordinal-browser/issues</url>
  <provides>
    <binary>ordinal-browser</binary>
  </provides>
  <releases>
    <release version="${VERSION}" date="2026-02-13"/>
  </releases>
  <content_rating type="oars-1.1"/>
</component>
APPDATA

# ---- linuxdeploy 다운로드 및 실행 ----
echo ""
echo "📦 AppImage 빌드 중..."

LINUXDEPLOY="${BUILD_DIR}/linuxdeploy-${ARCH}.AppImage"
LINUXDEPLOY_QT="${BUILD_DIR}/linuxdeploy-plugin-qt-${ARCH}.AppImage"

# linuxdeploy 다운로드 (없으면)
if [ ! -f "${LINUXDEPLOY}" ]; then
    echo "📥 linuxdeploy 다운로드 중..."
    DEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"
    curl -L -o "${LINUXDEPLOY}" "${DEPLOY_URL}" 2>/dev/null || \
        wget -q -O "${LINUXDEPLOY}" "${DEPLOY_URL}" 2>/dev/null || {
        echo "⚠️  linuxdeploy 다운로드 실패. 수동 AppImage 빌드로 전환합니다."
        LINUXDEPLOY=""
    }
    if [ -f "${LINUXDEPLOY}" ]; then
        chmod +x "${LINUXDEPLOY}"
    fi
fi

# Qt 플러그인 다운로드 (없으면)
if [ ! -f "${LINUXDEPLOY_QT}" ]; then
    echo "📥 linuxdeploy Qt 플러그인 다운로드 중..."
    QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage"
    curl -L -o "${LINUXDEPLOY_QT}" "${QT_URL}" 2>/dev/null || \
        wget -q -O "${LINUXDEPLOY_QT}" "${QT_URL}" 2>/dev/null || {
        echo "⚠️  linuxdeploy Qt 플러그인 다운로드 실패."
        LINUXDEPLOY_QT=""
    }
    if [ -f "${LINUXDEPLOY_QT}" ]; then
        chmod +x "${LINUXDEPLOY_QT}"
    fi
fi

# AppImage 생성
OUTPUT="${BUILD_DIR}/${APPIMAGE_NAME}"
rm -f "${OUTPUT}"

if [ -n "${LINUXDEPLOY}" ] && [ -f "${LINUXDEPLOY}" ]; then
    echo "🔧 linuxdeploy로 AppImage 생성 중..."

    export LDAI_OUTPUT="${OUTPUT}"
    export LDAI_UPDATE_INFORMATION=""

    # linuxdeploy 실행 (Qt 플러그인 포함)
    if [ -n "${LINUXDEPLOY_QT}" ] && [ -f "${LINUXDEPLOY_QT}" ]; then
        "${LINUXDEPLOY}" \
            --appdir "${APPDIR}" \
            --plugin qt \
            --output appimage \
            2>&1 | tail -10 || true
    else
        "${LINUXDEPLOY}" \
            --appdir "${APPDIR}" \
            --output appimage \
            2>&1 | tail -10 || true
    fi
else
    # 수동 AppImage 빌드 (appimagetool 사용)
    echo "🔧 수동 AppImage 빌드 중..."
    APPIMAGETOOL="${BUILD_DIR}/appimagetool-${ARCH}.AppImage"

    if [ ! -f "${APPIMAGETOOL}" ]; then
        echo "📥 appimagetool 다운로드 중..."
        TOOL_URL="https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${ARCH}.AppImage"
        curl -L -o "${APPIMAGETOOL}" "${TOOL_URL}" 2>/dev/null || \
            wget -q -O "${APPIMAGETOOL}" "${TOOL_URL}" 2>/dev/null
        chmod +x "${APPIMAGETOOL}" 2>/dev/null || true
    fi

    if [ -f "${APPIMAGETOOL}" ]; then
        ARCH="${ARCH}" "${APPIMAGETOOL}" "${APPDIR}" "${OUTPUT}" 2>&1 | tail -5 || true
    else
        echo "⚠️  appimagetool을 사용할 수 없습니다. AppDir만 생성합니다."
    fi
fi

# ---- 정리 ----
# AppDir은 디버깅용으로 보존 (선택적으로 삭제)
# rm -rf "${APPDIR}"

# ---- 결과 출력 ----
echo ""
echo "============================================================"
if [ -f "${OUTPUT}" ]; then
    chmod +x "${OUTPUT}"
    echo "✅ AppImage 빌드 완료!"
    echo ""
    echo "📦 파일: ${OUTPUT}"
    echo "📏 크기: $(du -h "${OUTPUT}" | cut -f1)"
    echo ""
    echo "실행 방법:"
    echo "  chmod +x ${APPIMAGE_NAME}"
    echo "  ./${APPIMAGE_NAME}"
else
    echo "⚠️  AppImage 파일이 생성되지 않았습니다."
    echo "   AppDir은 준비되었습니다: ${APPDIR}"
    echo "   수동 빌드: appimagetool ${APPDIR} ${OUTPUT}"
fi
echo "============================================================"
