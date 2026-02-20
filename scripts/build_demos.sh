#!/bin/bash
# Build native binaries for all demos using viper project format
# Usage: ./scripts/build_demos.sh [--clean]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
BIN_DIR="$ROOT_DIR/demos/bin"
BASIC_DIR="$ROOT_DIR/demos/basic"
ZIA_DIR="$ROOT_DIR/demos/zia"

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

# Check architecture
ARCH=$(uname -m)
if [[ "$ARCH" != "arm64" ]]; then
    echo -e "${YELLOW}Warning: This script builds ARM64 binaries but running on $ARCH${NC}"
    echo "Binaries may not run on this machine"
fi

# Create directories
mkdir -p "$BIN_DIR"

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning existing binaries..."
    rm -f "$BIN_DIR"/*
fi

# Demo configurations: name:project_dir
BASIC_DEMOS=(
    "chess:${BASIC_DIR}/chess"
    "vtris:${BASIC_DIR}/vtris"
    "frogger:${BASIC_DIR}/frogger"
    "centipede:${BASIC_DIR}/centipede"
    "pacman:${BASIC_DIR}/pacman"
)

ZIA_DEMOS=(
    "paint:${ZIA_DIR}/paint"
    "viperide:${ZIA_DIR}/viperide"
    "pacman-zia:${ZIA_DIR}/pacman"
    "sqldb:${ZIA_DIR}/sqldb"
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
        ((SUCCEEDED++))
    else
        ((FAILED++))
    fi
    echo ""
done

echo "=== Zia Demos ==="
echo ""

for demo in "${ZIA_DEMOS[@]}"; do
    IFS=':' read -r name project_dir <<< "$demo"
    echo "Building $name..."

    if build_demo "$name" "$project_dir"; then
        ((SUCCEEDED++))
    else
        ((FAILED++))
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
