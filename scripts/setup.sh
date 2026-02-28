#!/bin/bash
# ============================================================
# OrdinalV8 개발 환경 설정 스크립트
# ============================================================
#
# macOS 개발 환경에 필요한 의존성을 설치하고
# Python 가상 환경을 구성합니다.
#
# 사전 요구 사항:
#   - macOS (Apple Silicon 또는 Intel)
#   - Homebrew (https://brew.sh)
#
# 사용법:
#   chmod +x scripts/setup.sh
#   ./scripts/setup.sh
# ============================================================

set -euo pipefail

# 색상 출력
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 프로젝트 루트 디렉토리
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo -e "${GREEN}╔══════════════════════════════════════╗${NC}"
echo -e "${GREEN}║   OrdinalV8 개발 환경 설정    ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════╝${NC}"
echo ""

# ============================================================
# 1단계: macOS 확인
# ============================================================

echo -e "${YELLOW}[1/5] 시스템 확인${NC}"

if [[ "$(uname)" != "Darwin" ]]; then
    echo -e "${RED}오류: 이 스크립트는 macOS에서만 실행 가능합니다.${NC}"
    exit 1
fi

ARCH=$(uname -m)
OS_VERSION=$(sw_vers -productVersion)
echo "macOS ${OS_VERSION} (${ARCH}) 감지"

# Homebrew 확인
if ! command -v brew &>/dev/null; then
    echo -e "${RED}오류: Homebrew가 설치되어 있지 않습니다.${NC}"
    echo "설치: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
    exit 1
fi
echo "Homebrew $(brew --version | head -1 | awk '{print $2}') 감지"

# ============================================================
# 2단계: Homebrew 의존성 설치
# ============================================================

echo -e "\n${YELLOW}[2/5] Homebrew 의존성 설치${NC}"

BREW_PACKAGES=(
    qt@6        # UI 프레임워크
    cmake       # 빌드 시스템
    openssl@3   # SSL/TLS 라이브러리
    curl        # HTTP 클라이언트
    grpc        # gRPC C++ 라이브러리
    protobuf    # Protocol Buffers
    python@3.12 # Python 인터프리터
    ninja       # 고속 빌드 시스템
)

echo "설치할 패키지: ${BREW_PACKAGES[*]}"

for pkg in "${BREW_PACKAGES[@]}"; do
    if brew list "${pkg}" &>/dev/null; then
        echo -e "  ${GREEN}✓${NC} ${pkg} (이미 설치됨)"
    else
        echo -e "  ${BLUE}↓${NC} ${pkg} 설치 중..."
        brew install "${pkg}" 2>/dev/null || {
            echo -e "  ${RED}✗${NC} ${pkg} 설치 실패 — 계속 진행"
        }
    fi
done

# ============================================================
# 3단계: 환경 변수 설정
# ============================================================

echo -e "\n${YELLOW}[3/5] 환경 변수 설정${NC}"

# Qt6 경로
QT_PREFIX="$(brew --prefix qt@6 2>/dev/null || echo "")"
if [ -n "${QT_PREFIX}" ]; then
    export CMAKE_PREFIX_PATH="${QT_PREFIX}"
    echo "Qt6 경로: ${QT_PREFIX}"
fi

# OpenSSL 경로
OPENSSL_PREFIX="$(brew --prefix openssl@3 2>/dev/null || echo "")"
if [ -n "${OPENSSL_PREFIX}" ]; then
    export OPENSSL_ROOT_DIR="${OPENSSL_PREFIX}"
    echo "OpenSSL 경로: ${OPENSSL_PREFIX}"
fi

# ============================================================
# 4단계: Python 가상 환경 생성
# ============================================================

echo -e "\n${YELLOW}[4/5] Python 가상 환경 설정${NC}"

cd "${PROJECT_ROOT}"

VENV_DIR="${PROJECT_ROOT}/.venv"
PYTHON_BIN="python3.12"

# python3.12 없으면 python3 사용
if ! command -v "${PYTHON_BIN}" &>/dev/null; then
    PYTHON_BIN="python3"
fi

if [ -d "${VENV_DIR}" ]; then
    echo "가상 환경이 이미 존재합니다: ${VENV_DIR}"
else
    echo "${PYTHON_BIN}으로 가상 환경 생성 중..."
    ${PYTHON_BIN} -m venv "${VENV_DIR}"
    echo "가상 환경 생성 완료: ${VENV_DIR}"
fi

# 가상 환경 활성화
echo "가상 환경 활성화..."
source "${VENV_DIR}/bin/activate"

# pip 업그레이드
echo "pip 업그레이드 중..."
pip install --upgrade pip --quiet

# 의존성 설치
echo "Python 의존성 설치 중..."
if [ -f "${PROJECT_ROOT}/agent/requirements.txt" ]; then
    pip install -r "${PROJECT_ROOT}/agent/requirements.txt" --quiet
    echo "agent/requirements.txt 설치 완료"
else
    echo -e "${RED}경고: agent/requirements.txt를 찾을 수 없습니다${NC}"
fi

# ============================================================
# 5단계: 빌드 디렉토리 생성
# ============================================================

echo -e "\n${YELLOW}[5/5] 빌드 디렉토리 설정${NC}"

BUILD_DIR="${PROJECT_ROOT}/build"
mkdir -p "${BUILD_DIR}"

echo "빌드 디렉토리: ${BUILD_DIR}"

# 데이터 디렉토리 생성
DATA_DIR="${PROJECT_ROOT}/data"
mkdir -p "${DATA_DIR}/blocklists"
mkdir -p "${DATA_DIR}/embeddings_cache"
echo "데이터 디렉토리: ${DATA_DIR}"

# ============================================================
# 완료
# ============================================================

echo ""
echo -e "${GREEN}╔══════════════════════════════════════╗${NC}"
echo -e "${GREEN}║        설정 완료!                    ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════╝${NC}"
echo ""
echo "다음 단계:"
echo ""
echo "  1. 가상 환경 활성화:"
echo "     source .venv/bin/activate"
echo ""
echo "  2. C++ 빌드:"
echo "     cd build && cmake .. && make -j\$(sysctl -n hw.ncpu)"
echo ""
echo "  3. V8 엔진 빌드 (선택):"
echo "     ./scripts/build_v8.sh"
echo ""
echo "  4. 보안 에이전트 실행:"
echo "     python -m agent"
echo ""
echo "  5. 브라우저 실행:"
echo "     ./build/ordinalv8"
echo ""
