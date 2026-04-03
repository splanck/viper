#!/bin/bash
# Build native binaries for all demos using viper project format
# Usage: ./scripts/build_demos.sh [--clean]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
BIN_DIR="$ROOT_DIR/examples/bin"
GAMES_DIR="$ROOT_DIR/examples/games"
APPS_DIR="$ROOT_DIR/examples/apps"

VIPER="$BUILD_DIR/src/tools/viper/viper"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

usage() {
    echo "Usage: $0 [--clean]"
    echo "  --clean    Remove existing binaries before building"
    exit 1
}

# Parse arguments
CLEAN=0
for arg in "$@"; do
    case $arg in
        --clean)
            CLEAN=1
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown argument: $arg"
            usage
            ;;
    esac
done

# Check prerequisites
if [[ ! -x "$VIPER" ]]; then
    echo -e "${RED}Error: viper tool not found at $VIPER${NC}"
    echo "Run 'cmake --build build' first"
    exit 1
fi

# Detect architecture
ARCH=$(uname -m)
case "$ARCH" in
    arm64|aarch64) ARCH_FLAG="--arch arm64" ;;
    x86_64|amd64)  ARCH_FLAG="--arch x64"   ;;
    *)
        echo -e "${YELLOW}Warning: Unknown architecture $ARCH — defaulting to host${NC}"
        ARCH_FLAG=""
        ;;
esac

# Create directories
mkdir -p "$BIN_DIR"

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning existing binaries..."
    rm -f "$BIN_DIR"/*
fi

# Demo configurations: name:project_dir
BASIC_DEMOS=(
    "vtris:${GAMES_DIR}/vtris"
    "frogger:${GAMES_DIR}/frogger-basic"
    "centipede:${GAMES_DIR}/centipede-basic"
)

ZIA_DEMOS=(
    "paint:${APPS_DIR}/paint"
    "viperide:${APPS_DIR}/viperide"
    "3dbowling:${GAMES_DIR}/3dbowling"
    "pacman-zia:${GAMES_DIR}/pacman"
    "sqldb:${APPS_DIR}/sqldb"
    "chess-zia:${GAMES_DIR}/chess"
    "sidescroller:${GAMES_DIR}/sidescroller"
)

build_demo() {
    local name="$1"
    local project_dir="$2"
    local exe_file="$BIN_DIR/${name}"

    if [[ ! -f "$project_dir/viper.project" ]]; then
        echo -e "${RED}  Error: No viper.project found in $project_dir${NC}"
        return 1
    fi

    echo -n "  Compiling... "
    if ! "$VIPER" build "$project_dir" -o "$exe_file" 2>/tmp/viper_build_err_$$; then
        echo -e "${RED}FAILED${NC}"
        head -20 /tmp/viper_build_err_$$
        rm -f /tmp/viper_build_err_$$
        return 1
    fi
    rm -f /tmp/viper_build_err_$$
    echo -e "${GREEN}OK${NC}"

    local size=$(ls -lh "$exe_file" | awk '{print $5}')
    echo -e "  ${GREEN}Built: $exe_file ($size)${NC}"
    return 0
}

echo "Building Viper demos as native binaries"
echo "=============================================="
echo ""

FAILED=0
SUCCEEDED=0

echo "=== BASIC Demos ==="
echo ""

for demo in "${BASIC_DEMOS[@]}"; do
    IFS=':' read -r name project_dir <<< "$demo"
    echo "Building $name..."

    if build_demo "$name" "$project_dir"; then
        SUCCEEDED=$((SUCCEEDED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
    echo ""
done

echo "=== Zia Demos ==="
echo ""

for demo in "${ZIA_DEMOS[@]}"; do
    IFS=':' read -r name project_dir <<< "$demo"
    echo "Building $name..."

    if build_demo "$name" "$project_dir"; then
        SUCCEEDED=$((SUCCEEDED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
    echo ""
done

echo "=============================================="
if [[ $FAILED -eq 0 ]]; then
    echo -e "${GREEN}All $SUCCEEDED demos built successfully!${NC}"
    echo ""
    echo "Binaries are in: $BIN_DIR"
    ls -lh "$BIN_DIR"
else
    echo -e "${RED}$FAILED demo(s) failed, $SUCCEEDED succeeded${NC}"
    exit 1
fi
