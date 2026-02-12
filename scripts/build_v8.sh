#!/bin/bash
# ============================================================
# V8 엔진 빌드 스크립트 (macOS ARM64)
# ============================================================
#
# Google V8 JavaScript 엔진을 macOS ARM64 (Apple Silicon)
# 환경에서 모놀리식 정적 라이브러리로 빌드합니다.
#
# 사전 요구 사항:
#   - Xcode Command Line Tools
#   - Python 3
#   - Git
#
# 사용법:
#   chmod +x scripts/build_v8.sh
#   ./scripts/build_v8.sh
#
# 출력:
#   third_party/v8/out/arm64.release/obj/libv8_monolith.a
# ============================================================

set -euo pipefail

# 색상 출력
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # 색상 리셋

# 프로젝트 루트 디렉토리
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THIRD_PARTY="${PROJECT_ROOT}/third_party"
V8_DIR="${THIRD_PARTY}/v8"
DEPOT_TOOLS_DIR="${THIRD_PARTY}/depot_tools"

echo -e "${GREEN}[V8 빌드] macOS ARM64 V8 엔진 빌드 시작${NC}"
echo "[V8 빌드] 프로젝트 루트: ${PROJECT_ROOT}"

# ============================================================
# 1단계: depot_tools 설치
# ============================================================

echo -e "\n${YELLOW}[1/5] depot_tools 클론${NC}"

mkdir -p "${THIRD_PARTY}"

if [ -d "${DEPOT_TOOLS_DIR}" ]; then
    echo "depot_tools가 이미 존재합니다. 업데이트 중..."
    cd "${DEPOT_TOOLS_DIR}"
    git pull --quiet
else
    echo "depot_tools 클론 중..."
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git \
        "${DEPOT_TOOLS_DIR}"
fi

# depot_tools를 PATH에 추가
export PATH="${DEPOT_TOOLS_DIR}:${PATH}"
echo "depot_tools 경로 추가: ${DEPOT_TOOLS_DIR}"

# ============================================================
# 2단계: V8 소스 가져오기
# ============================================================

echo -e "\n${YELLOW}[2/5] V8 소스 가져오기 (fetch v8)${NC}"

if [ -d "${V8_DIR}" ]; then
    echo "V8 소스가 이미 존재합니다. 동기화 중..."
    cd "${V8_DIR}"
    git fetch --quiet
else
    echo "V8 소스 가져오기 중 (시간이 소요됩니다)..."
    mkdir -p "${V8_DIR}"
    cd "${THIRD_PARTY}"
    fetch v8
fi

# ============================================================
# 3단계: 의존성 동기화
# ============================================================

echo -e "\n${YELLOW}[3/5] 의존성 동기화 (gclient sync)${NC}"

cd "${V8_DIR}"
gclient sync --quiet

# ============================================================
# 4단계: 빌드 설정 (gn gen)
# ============================================================

echo -e "\n${YELLOW}[4/5] 빌드 설정 생성 (ARM64 Release)${NC}"

BUILD_DIR="out/arm64.release"

# GN 빌드 인수
GN_ARGS='target_cpu="arm64"
is_debug=false
is_component_build=false
v8_monolithic=true
v8_use_external_startup_data=false
use_custom_libcxx=false
v8_enable_i18n_support=true
treat_warnings_as_errors=false
symbol_level=0
v8_enable_sandbox=true'

echo "GN 빌드 인수:"
echo "${GN_ARGS}"

gn gen "${BUILD_DIR}" --args="${GN_ARGS}"

echo "빌드 디렉토리: ${V8_DIR}/${BUILD_DIR}"

# ============================================================
# 5단계: 빌드 실행 (ninja)
# ============================================================

echo -e "\n${YELLOW}[5/5] V8 모놀리식 빌드 (ninja)${NC}"

# CPU 코어 수에 맞춰 병렬 빌드
NPROC=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
echo "병렬 빌드 작업 수: ${NPROC}"

ninja -C "${BUILD_DIR}" v8_monolith -j"${NPROC}"

# ============================================================
# 빌드 결과 확인
# ============================================================

MONOLITH_LIB="${V8_DIR}/${BUILD_DIR}/obj/libv8_monolith.a"

if [ -f "${MONOLITH_LIB}" ]; then
    LIB_SIZE=$(du -h "${MONOLITH_LIB}" | cut -f1)
    echo -e "\n${GREEN}[V8 빌드] 빌드 성공!${NC}"
    echo "출력: ${MONOLITH_LIB}"
    echo "크기: ${LIB_SIZE}"
    echo ""
    echo "CMake에서 사용하려면:"
    echo "  cmake -DV8_ROOT=${V8_DIR} .."
else
    echo -e "\n${RED}[V8 빌드] 빌드 실패 — libv8_monolith.a를 찾을 수 없습니다${NC}"
    exit 1
fi
