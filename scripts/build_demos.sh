#!/bin/bash
# Build native ARM64 binaries for all demos
# Usage: ./scripts/build_demos.sh [--clean]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
BIN_DIR="$ROOT_DIR/demos/bin"
BASIC_DIR="$ROOT_DIR/demos/basic"
TMP_DIR="/tmp/viper_demo_build_$$"

ILC="$BUILD_DIR/src/tools/viper/viper"
ZIA="$BUILD_DIR/src/tools/zia/zia"
RUNTIME_LIB="$BUILD_DIR/src/runtime/libviper_runtime.a"
GFX_LIB="$BUILD_DIR/lib/libvipergfx.a"
GUI_LIB="$BUILD_DIR/src/lib/gui/libvipergui.a"
ZIA_DIR="$ROOT_DIR/demos/zia"

# macOS frameworks needed for graphics
MACOS_GFX_FRAMEWORKS="-framework Cocoa -framework IOKit -framework CoreFoundation"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

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
if [[ ! -x "$ILC" ]]; then
    echo -e "${RED}Error: viper tool not found at $ILC${NC}"
    echo "Run 'cmake --build build' first"
    exit 1
fi

if [[ ! -f "$RUNTIME_LIB" ]]; then
    echo -e "${RED}Error: Runtime library not found at $RUNTIME_LIB${NC}"
    echo "Run 'cmake --build build' first"
    exit 1
fi

if [[ ! -x "$ZIA" ]]; then
    echo -e "${RED}Error: zia not found at $ZIA${NC}"
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
mkdir -p "$TMP_DIR"

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning existing binaries..."
    rm -f "$BIN_DIR"/*
fi

# Demo configurations: name:source_path
BASIC_DEMOS=(
    "chess:${BASIC_DIR}/chess/chess.bas"
    "vtris:${BASIC_DIR}/vtris/vtris.bas"
    "frogger:${BASIC_DIR}/frogger/frogger.bas"
    "centipede:${BASIC_DIR}/centipede/centipede.bas"
    "pacman:${BASIC_DIR}/pacman/pacman.bas"
)

# Zia demo configurations: name:source_path
ZIA_DEMOS=(
    "paint:${ZIA_DIR}/paint/main.zia"
    "viperide:${ZIA_DIR}/viperide/main.zia"
    "pacman-zia:${ZIA_DIR}/pacman/main_modular.zia"
)

build_basic_demo() {
    local name="$1"
    local source="$2"
    local source_path="$source"

    if [[ ! -f "$source_path" ]]; then
        echo -e "${RED}  Error: Source file not found: $source_path${NC}"
        return 1
    fi

    local il_file="$TMP_DIR/${name}.il"
    local asm_file="$TMP_DIR/${name}.s"
    local obj_file="$TMP_DIR/${name}.o"
    local exe_file="$BIN_DIR/${name}"

    echo -n "  Compiling BASIC to IL... "
    if ! "$ILC" front basic -emit-il "$source_path" > "$il_file" 2>/dev/null; then
        echo -e "${RED}FAILED${NC}"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    echo -n "  Generating ARM64 assembly... "
    if ! "$ILC" codegen arm64 "$il_file" -S "$asm_file" 2>/dev/null; then
        echo -e "${RED}FAILED${NC}"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    echo -n "  Assembling... "
    if ! as "$asm_file" -o "$obj_file" 2>&1; then
        echo -e "${RED}FAILED${NC}"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    echo -n "  Linking... "
    if ! clang++ "$obj_file" "$RUNTIME_LIB" -o "$exe_file" 2>&1; then
        echo -e "${RED}FAILED${NC}"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    local size=$(ls -lh "$exe_file" | awk '{print $5}')
    echo -e "  ${GREEN}Built: $exe_file ($size)${NC}"
    return 0
}

build_zia_demo() {
    local name="$1"
    local source="$2"
    local source_path="$source"

    if [[ ! -f "$source_path" ]]; then
        echo -e "${RED}  Error: Source file not found: $source_path${NC}"
        return 1
    fi

    local il_file="$TMP_DIR/${name}.il"
    local asm_file="$TMP_DIR/${name}.s"
    local obj_file="$TMP_DIR/${name}.o"
    local exe_file="$BIN_DIR/${name}"

    echo -n "  Compiling Zia to IL... "
    if ! "$ZIA" "$source_path" --emit-il > "$il_file" 2>/dev/null; then
        echo -e "${RED}FAILED${NC}"
        # Show the actual error
        "$ZIA" "$source_path" --emit-il 2>&1 | head -20
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    echo -n "  Generating ARM64 assembly... "
    if ! "$ILC" codegen arm64 "$il_file" -S "$asm_file" 2>/dev/null; then
        echo -e "${RED}FAILED${NC}"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    echo -n "  Assembling... "
    if ! as "$asm_file" -o "$obj_file" 2>&1; then
        echo -e "${RED}FAILED${NC}"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    echo -n "  Linking... "
    # Zia demos typically need graphics libraries
    if ! clang++ "$obj_file" "$RUNTIME_LIB" "$GFX_LIB" "$GUI_LIB" $MACOS_GFX_FRAMEWORKS -o "$exe_file" 2>&1; then
        echo -e "${RED}FAILED${NC}"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    local size=$(ls -lh "$exe_file" | awk '{print $5}')
    echo -e "  ${GREEN}Built: $exe_file ($size)${NC}"
    return 0
}

echo "Building Viper demos as native ARM64 binaries"
echo "=============================================="
echo ""

FAILED=0
SUCCEEDED=0

echo "=== BASIC Demos ==="
echo ""

for demo in "${BASIC_DEMOS[@]}"; do
    IFS=':' read -r name source <<< "$demo"
    echo "Building $name..."

    if build_basic_demo "$name" "$source"; then
        ((SUCCEEDED++))
    else
        ((FAILED++))
    fi
    echo ""
done

echo "=== Zia Demos ==="
echo ""

for demo in "${ZIA_DEMOS[@]}"; do
    IFS=':' read -r name source <<< "$demo"
    echo "Building $name..."

    if build_zia_demo "$name" "$source"; then
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
