#!/bin/bash

# ============================================================
# macOS DMG 패키징 스크립트
# ============================================================
# Ordinal Browser를 .app 번들로 만들고 DMG 디스크 이미지로 패키징합니다.
#
# 사용법:
#   ./packaging/macos/create_dmg.sh [빌드_디렉토리]
#
# 필요한 도구:
#   - macdeployqt (Qt 배포 도구)
#   - hdiutil (macOS 디스크 이미지 도구)
#   - codesign (코드 서명 도구)
# ============================================================

set -euo pipefail

# ---- 설정 변수 ----
APP_NAME="Ordinal Browser"
APP_BUNDLE="${APP_NAME}.app"
BINARY_NAME="ordinal-browser"
VERSION=$(cat "$(dirname "$0")/../../VERSION" 2>/dev/null || echo "1.0.0")
BUNDLE_ID="com.kaztoray.ordinal-browser"
DMG_NAME="OrdinalBrowser-${VERSION}-macOS"
BUILD_DIR="${1:-$(dirname "$0")/../../build}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

echo "🍎 Ordinal Browser macOS 패키징 시작 (v${VERSION})"
echo "============================================================"

# ---- 빌드 디렉토리 확인 ----
if [ ! -f "${BUILD_DIR}/${BINARY_NAME}" ]; then
    echo "❌ 바이너리를 찾을 수 없습니다: ${BUILD_DIR}/${BINARY_NAME}"
    echo "   먼저 빌드를 실행하세요: mkdir build && cd build && cmake .. && make -j\$(sysctl -n hw.ncpu)"
    exit 1
fi

echo "📁 빌드 디렉토리: ${BUILD_DIR}"
echo "📦 출력 DMG: ${DMG_NAME}.dmg"

# ---- .app 번들 생성 ----
echo ""
echo "📂 .app 번들 생성 중..."
APP_PATH="${BUILD_DIR}/${APP_BUNDLE}"

# 기존 번들 삭제
rm -rf "${APP_PATH}"

# 디렉토리 구조 생성
mkdir -p "${APP_PATH}/Contents/MacOS"
mkdir -p "${APP_PATH}/Contents/Resources"
mkdir -p "${APP_PATH}/Contents/Frameworks"

# ---- 바이너리 복사 ----
echo "🔧 바이너리 복사 중..."
cp "${BUILD_DIR}/${BINARY_NAME}" "${APP_PATH}/Contents/MacOS/${BINARY_NAME}"
chmod +x "${APP_PATH}/Contents/MacOS/${BINARY_NAME}"

# ---- Info.plist 생성 ----
echo "📋 Info.plist 생성 중..."
if [ -f "${SCRIPT_DIR}/Info.plist" ]; then
    # 템플릿 Info.plist에서 버전 치환
    sed "s/\${VERSION}/${VERSION}/g" "${SCRIPT_DIR}/Info.plist" > "${APP_PATH}/Contents/Info.plist"
else
    # Info.plist 인라인 생성
    cat > "${APP_PATH}/Contents/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
    "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>${BUNDLE_ID}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleExecutable</key>
    <string>${BINARY_NAME}</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>LSMinimumSystemVersion</key>
    <string>13.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.productivity</string>
    <key>CFBundleURLTypes</key>
    <array>
        <dict>
            <key>CFBundleURLName</key>
            <string>Web URL</string>
            <key>CFBundleURLSchemes</key>
            <array>
                <string>http</string>
                <string>https</string>
            </array>
        </dict>
    </array>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeName</key>
            <string>HTML Document</string>
            <key>CFBundleTypeRole</key>
            <string>Viewer</string>
            <key>LSItemContentTypes</key>
            <array>
                <string>public.html</string>
            </array>
        </dict>
    </array>
    <key>NSAppTransportSecurity</key>
    <dict>
        <key>NSAllowsArbitraryLoads</key>
        <true/>
    </dict>
</dict>
</plist>
PLIST
fi

# ---- PkgInfo 생성 ----
echo "APPL????" > "${APP_PATH}/Contents/PkgInfo"

# ---- macdeployqt 실행 (Qt 프레임워크 번들링) ----
echo "🔗 Qt 프레임워크 번들링 중 (macdeployqt)..."
if command -v macdeployqt &> /dev/null; then
    macdeployqt "${APP_PATH}" \
        -verbose=1 \
        -always-overwrite \
        2>&1 | tail -5 || echo "⚠️  macdeployqt 경고 무시 (일부 플러그인 누락 가능)"
else
    echo "⚠️  macdeployqt를 찾을 수 없습니다. Qt 프레임워크를 수동으로 번들링하세요."
    echo "   brew install qt6 또는 Qt 설치 경로를 PATH에 추가하세요."
fi

# ---- V8 라이브러리 복사 ----
echo "📚 V8 라이브러리 복사 중..."
V8_LIB_DIR="${PROJECT_ROOT}/third_party/v8/lib"
if [ -d "${V8_LIB_DIR}" ]; then
    # V8 동적 라이브러리 복사
    find "${V8_LIB_DIR}" -name "*.dylib" -exec cp {} "${APP_PATH}/Contents/Frameworks/" \;
    # RPATH 갱신 (번들 내부 경로로)
    for dylib in "${APP_PATH}/Contents/Frameworks/"*.dylib; do
        if [ -f "$dylib" ]; then
            install_name_tool -id "@executable_path/../Frameworks/$(basename "$dylib")" "$dylib" 2>/dev/null || true
        fi
    done
    echo "   ✅ V8 라이브러리 복사 완료"
else
    echo "   ⚠️  V8 라이브러리 디렉토리를 찾을 수 없습니다: ${V8_LIB_DIR}"
    echo "   CI 환경에서는 V8이 빌드 시 정적 링크될 수 있습니다."
fi

# ---- 코드 서명 ----
echo "🔏 코드 서명 중..."
codesign --deep --force --sign - "${APP_PATH}" 2>/dev/null || {
    echo "⚠️  코드 서명 실패. 배포 시 유효한 인증서로 서명하세요."
    echo "   개발용 ad-hoc 서명으로 진행합니다."
}

# 서명 검증
echo "🔍 코드 서명 검증 중..."
codesign --verify --deep --strict "${APP_PATH}" 2>/dev/null && \
    echo "   ✅ 코드 서명 유효" || \
    echo "   ⚠️  코드 서명 검증 실패 (개발 빌드에서는 정상)"

# ---- DMG 생성 ----
echo ""
echo "💿 DMG 디스크 이미지 생성 중..."

DMG_TEMP="${BUILD_DIR}/${DMG_NAME}-temp.dmg"
DMG_FINAL="${BUILD_DIR}/${DMG_NAME}.dmg"
DMG_VOLUME="/Volumes/${APP_NAME}"
DMG_SIZE="500m"

# 기존 DMG 정리
rm -f "${DMG_TEMP}" "${DMG_FINAL}"

# 임시 DMG 생성 (읽기/쓰기)
hdiutil create \
    -size "${DMG_SIZE}" \
    -fs HFS+ \
    -volname "${APP_NAME}" \
    "${DMG_TEMP}" \
    -quiet

# DMG 마운트
echo "📀 DMG 마운트 중..."
MOUNT_OUTPUT=$(hdiutil attach "${DMG_TEMP}" -readwrite -noverify -noautoopen 2>&1)
MOUNT_POINT=$(echo "${MOUNT_OUTPUT}" | grep -o '/Volumes/.*' | head -1)

if [ -z "${MOUNT_POINT}" ]; then
    echo "❌ DMG 마운트 실패"
    echo "${MOUNT_OUTPUT}"
    exit 1
fi

echo "   마운트 포인트: ${MOUNT_POINT}"

# .app 번들 복사
echo "📦 .app 번들 복사 중..."
cp -R "${APP_PATH}" "${MOUNT_POINT}/"

# Applications 심볼릭 링크 생성 (드래그 앤 드롭 설치용)
echo "🔗 Applications 바로가기 생성 중..."
ln -sf /Applications "${MOUNT_POINT}/Applications"

# 볼륨 아이콘 위치 설정 (선택적)
# .DS_Store를 통해 Finder 레이아웃 설정 가능

# DMG 분리
echo "📤 DMG 분리 중..."
sync
hdiutil detach "${MOUNT_POINT}" -quiet -force 2>/dev/null || {
    sleep 2
    hdiutil detach "${MOUNT_POINT}" -quiet -force 2>/dev/null || true
}

# 읽기 전용 압축 DMG로 변환
echo "🗜️  압축 DMG 변환 중..."
hdiutil convert \
    "${DMG_TEMP}" \
    -format UDZO \
    -imagekey zlib-level=9 \
    -o "${DMG_FINAL}" \
    -quiet

# 임시 파일 정리
rm -f "${DMG_TEMP}"

# ---- 결과 출력 ----
echo ""
echo "============================================================"
echo "✅ macOS 패키징 완료!"
echo ""
echo "📦 DMG 파일: ${DMG_FINAL}"
echo "📏 크기: $(du -h "${DMG_FINAL}" | cut -f1)"
echo ""
echo "설치 방법:"
echo "  1. ${DMG_NAME}.dmg 더블클릭"
echo "  2. '${APP_NAME}'을 Applications 폴더로 드래그"
echo "  3. Applications에서 '${APP_NAME}' 실행"
echo ""
echo "배포 시 주의사항:"
echo "  - Apple Developer 인증서로 코드 서명 필요"
echo "  - 공증(Notarization): xcrun notarytool submit ${DMG_FINAL}"
echo "============================================================"
