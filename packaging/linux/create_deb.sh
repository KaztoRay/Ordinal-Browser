#!/bin/bash

# ============================================================
# Linux .deb 패키지 빌드 스크립트
# ============================================================
# OrdinalV8를 Debian/Ubuntu .deb 패키지로 빌드합니다.
#
# 사용법:
#   ./packaging/linux/create_deb.sh [빌드_디렉토리]
#
# 필요한 도구:
#   - dpkg-deb (Debian 패키지 빌더)
#   - fakeroot (선택 — 루트 권한 없이 빌드)
# ============================================================

set -euo pipefail

# ---- 설정 변수 ----
PACKAGE_NAME="ordinalv8"
VERSION=$(cat "$(dirname "$0")/../../VERSION" 2>/dev/null || echo "1.0.0")
ARCH=$(dpkg --print-architecture 2>/dev/null || echo "amd64")
MAINTAINER="KaztoRay <KaztoRay@users.noreply.github.com>"
DESCRIPTION="V8 기반 보안 브라우저 + LLM Security Agent"
BUILD_DIR="${1:-$(dirname "$0")/../../build}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DEB_DIR="${BUILD_DIR}/deb-package"
DEB_NAME="${PACKAGE_NAME}_${VERSION}_${ARCH}"

echo "🐧 OrdinalV8 .deb 패키지 빌드 시작 (v${VERSION}, ${ARCH})"
echo "============================================================"

# ---- 빌드 바이너리 확인 ----
if [ ! -f "${BUILD_DIR}/${PACKAGE_NAME}" ]; then
    echo "❌ 바이너리를 찾을 수 없습니다: ${BUILD_DIR}/${PACKAGE_NAME}"
    echo "   먼저 빌드를 실행하세요: mkdir build && cd build && cmake .. && make -j\$(nproc)"
    exit 1
fi

# ---- 기존 패키지 디렉토리 정리 ----
rm -rf "${DEB_DIR}"

# ---- 디렉토리 구조 생성 ----
echo "📂 패키지 디렉토리 구조 생성 중..."

# DEBIAN 제어 디렉토리
mkdir -p "${DEB_DIR}/DEBIAN"

# 바이너리 설치 경로
mkdir -p "${DEB_DIR}/usr/bin"

# 데스크톱 엔트리
mkdir -p "${DEB_DIR}/usr/share/applications"

# 아이콘 (다양한 크기)
mkdir -p "${DEB_DIR}/usr/share/icons/hicolor/48x48/apps"
mkdir -p "${DEB_DIR}/usr/share/icons/hicolor/128x128/apps"
mkdir -p "${DEB_DIR}/usr/share/icons/hicolor/256x256/apps"
mkdir -p "${DEB_DIR}/usr/share/icons/hicolor/scalable/apps"

# 문서
mkdir -p "${DEB_DIR}/usr/share/doc/${PACKAGE_NAME}"

# man 페이지
mkdir -p "${DEB_DIR}/usr/share/man/man1"

# MIME 타입
mkdir -p "${DEB_DIR}/usr/share/mime/packages"

# ---- DEBIAN/control 생성 ----
echo "📋 DEBIAN/control 생성 중..."

# 설치 크기 계산 (KB)
INSTALLED_SIZE=$(du -sk "${BUILD_DIR}/${PACKAGE_NAME}" | cut -f1)

cat > "${DEB_DIR}/DEBIAN/control" << CONTROL
Package: ${PACKAGE_NAME}
Version: ${VERSION}
Architecture: ${ARCH}
Maintainer: ${MAINTAINER}
Installed-Size: ${INSTALLED_SIZE}
Depends: libqt6widgets6 (>= 6.4), libqt6network6 (>= 6.4), libqt6gui6 (>= 6.4), libqt6core6 (>= 6.4), libcurl4 (>= 7.80), libssl3 (>= 3.0), libgrpc++1 (>= 1.40) | libgrpc++-dev (>= 1.40), ca-certificates
Recommends: fonts-noto, fonts-noto-cjk
Suggests: python3 (>= 3.12), python3-pip
Section: web
Priority: optional
Homepage: https://github.com/KaztoRay/ordinalv8
Description: ${DESCRIPTION}
 OrdinalV8는 V8 JavaScript 엔진 기반의 보안 중심 웹 브라우저입니다.
 .
 주요 기능:
  - V8 JavaScript 엔진을 통한 웹 페이지 렌더링
  - 실시간 피싱/악성코드/XSS 탐지
  - LLM 기반 위협 분석 에이전트 (GPT-4 + 로컬 모델)
  - 브라우저 핑거프린팅 방지 및 추적기 차단
  - SSL/TLS 인증서 심층 검증
  - 멀티탭 브라우징 및 개발자 도구
CONTROL

# ---- DEBIAN/postinst (설치 후 스크립트) ----
cat > "${DEB_DIR}/DEBIAN/postinst" << 'POSTINST'
#!/bin/bash
# 설치 후 데스크톱 데이터베이스 업데이트
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database /usr/share/applications 2>/dev/null || true
fi
# MIME 타입 데이터베이스 업데이트
if command -v update-mime-database &> /dev/null; then
    update-mime-database /usr/share/mime 2>/dev/null || true
fi
# 아이콘 캐시 업데이트
if command -v gtk-update-icon-cache &> /dev/null; then
    gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
fi
exit 0
POSTINST
chmod 755 "${DEB_DIR}/DEBIAN/postinst"

# ---- DEBIAN/postrm (제거 후 스크립트) ----
cat > "${DEB_DIR}/DEBIAN/postrm" << 'POSTRM'
#!/bin/bash
# 제거 후 데이터베이스 갱신
if command -v update-desktop-database &> /dev/null; then
    update-desktop-database /usr/share/applications 2>/dev/null || true
fi
if command -v update-mime-database &> /dev/null; then
    update-mime-database /usr/share/mime 2>/dev/null || true
fi
exit 0
POSTRM
chmod 755 "${DEB_DIR}/DEBIAN/postrm"

# ---- 바이너리 복사 ----
echo "🔧 바이너리 복사 중..."
cp "${BUILD_DIR}/${PACKAGE_NAME}" "${DEB_DIR}/usr/bin/${PACKAGE_NAME}"
chmod 755 "${DEB_DIR}/usr/bin/${PACKAGE_NAME}"

# ---- 데스크톱 파일 복사 ----
echo "🖥️  데스크톱 엔트리 복사 중..."
if [ -f "${SCRIPT_DIR}/ordinalv8.desktop" ]; then
    cp "${SCRIPT_DIR}/ordinalv8.desktop" \
        "${DEB_DIR}/usr/share/applications/${PACKAGE_NAME}.desktop"
else
    cat > "${DEB_DIR}/usr/share/applications/${PACKAGE_NAME}.desktop" << DESKTOP
[Desktop Entry]
Name=OrdinalV8
Comment=V8-based Security Browser with LLM Agent
Exec=ordinalv8 %u
Icon=ordinalv8
Type=Application
Categories=Network;WebBrowser;Security;
MimeType=text/html;x-scheme-handler/http;x-scheme-handler/https;
StartupNotify=true
DESKTOP
fi

# ---- 문서 복사 ----
echo "📄 문서 복사 중..."
if [ -f "${PROJECT_ROOT}/README.md" ]; then
    cp "${PROJECT_ROOT}/README.md" "${DEB_DIR}/usr/share/doc/${PACKAGE_NAME}/"
fi
if [ -f "${PROJECT_ROOT}/LICENSE" ]; then
    cp "${PROJECT_ROOT}/LICENSE" "${DEB_DIR}/usr/share/doc/${PACKAGE_NAME}/copyright"
fi
if [ -f "${PROJECT_ROOT}/CHANGELOG.md" ]; then
    cp "${PROJECT_ROOT}/CHANGELOG.md" "${DEB_DIR}/usr/share/doc/${PACKAGE_NAME}/"
fi

# ---- MIME 타입 정의 ----
cat > "${DEB_DIR}/usr/share/mime/packages/${PACKAGE_NAME}.xml" << MIME
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="x-scheme-handler/http">
    <comment>HTTP URL</comment>
  </mime-type>
  <mime-type type="x-scheme-handler/https">
    <comment>HTTPS URL</comment>
  </mime-type>
</mime-info>
MIME

# ---- .deb 빌드 ----
echo ""
echo "📦 .deb 패키지 빌드 중..."
if command -v fakeroot &> /dev/null; then
    fakeroot dpkg-deb --build "${DEB_DIR}" "${BUILD_DIR}/${DEB_NAME}.deb"
else
    dpkg-deb --build "${DEB_DIR}" "${BUILD_DIR}/${DEB_NAME}.deb"
fi

# ---- 패키지 검증 ----
echo ""
echo "🔍 패키지 검증 중..."
if command -v lintian &> /dev/null; then
    lintian "${BUILD_DIR}/${DEB_NAME}.deb" 2>/dev/null || true
fi

dpkg-deb --info "${BUILD_DIR}/${DEB_NAME}.deb" 2>/dev/null | head -15

# ---- 정리 ----
rm -rf "${DEB_DIR}"

# ---- 결과 출력 ----
echo ""
echo "============================================================"
echo "✅ .deb 패키지 빌드 완료!"
echo ""
echo "📦 패키지: ${BUILD_DIR}/${DEB_NAME}.deb"
echo "📏 크기: $(du -h "${BUILD_DIR}/${DEB_NAME}.deb" | cut -f1)"
echo ""
echo "설치 방법:"
echo "  sudo dpkg -i ${DEB_NAME}.deb"
echo "  sudo apt-get install -f  # 의존성 자동 해결"
echo ""
echo "또는:"
echo "  sudo apt install ./${DEB_NAME}.deb"
echo "============================================================"
