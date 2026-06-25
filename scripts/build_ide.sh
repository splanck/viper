#!/usr/bin/env bash
# Build ViperIDE as a standalone native binary.
# Usage: ./scripts/build_ide.sh [--clean] [--output PATH]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${VIPER_BUILD_DIR:-$ROOT_DIR/build}"
IDE_DIR="$ROOT_DIR/viperide"
OUT_DIR="${VIPER_IDE_OUT_DIR:-$IDE_DIR/bin}"
OUTPUT_FILE="${VIPER_IDE_OUTPUT:-$OUT_DIR/viperide}"

VIPER="$BUILD_DIR/src/tools/viper/viper"

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

usage() {
    echo "Usage: $0 [--clean] [--output PATH]"
    echo "  --clean        Remove the existing ViperIDE binary before building"
    echo "  --output PATH  Write the binary to PATH (default: viperide/bin/viperide)"
    exit 1
}

CLEAN=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)
            CLEAN=1
            shift
            ;;
        --output)
            if [[ $# -lt 2 ]]; then
                echo "Error: --output requires a path"
                usage
            fi
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Unknown argument: $1"
            usage
            ;;
    esac
done

if [[ ! -d "$IDE_DIR" ]]; then
    echo -e "${RED}Error: ViperIDE source not found at $IDE_DIR${NC}"
    exit 1
fi

if [[ ! -x "$VIPER" ]]; then
    echo -e "${RED}Error: viper tool not found at $VIPER${NC}"
    echo "Run './scripts/build_viper_mac.sh' or './scripts/build_viper_linux.sh' first"
    exit 1
fi

mkdir -p "$(dirname "$OUTPUT_FILE")"

if [[ $CLEAN -eq 1 ]]; then
    rm -f "$OUTPUT_FILE"
fi

TMP_BASE="/tmp/viperide_build_$$"
FRONTEND_ERR="${TMP_BASE}.front.err"

cleanup() {
    rm -f "$FRONTEND_ERR"
}
trap cleanup EXIT

build_macos() {
    local target_arch
    case "$(uname -m)" in
        arm64|aarch64)
            target_arch="arm64"
            ;;
        x86_64|amd64)
            target_arch="x64"
            ;;
        *)
            echo -e "${RED}Error: build_ide.sh currently supports x86_64 and arm64 macOS only${NC}"
            return 1
            ;;
    esac
    build_native "$target_arch"
}

build_linux() {
    local target_arch
    case "$(uname -m)" in
        x86_64|amd64)
            target_arch="x64"
            ;;
        aarch64|arm64)
            target_arch="arm64"
            ;;
        *)
            echo -e "${RED}Error: build_ide.sh currently supports x86_64 and arm64 Linux only${NC}"
            return 1
            ;;
    esac
    build_native "$target_arch"
}

build_native() {
    local target_arch="$1"
    echo -n "  Viper build (--arch $target_arch)... "
    if ! "$VIPER" build "$IDE_DIR" --arch "$target_arch" -o "$OUTPUT_FILE" 2>"$FRONTEND_ERR"; then
        echo -e "${RED}FAILED${NC}"
        head -40 "$FRONTEND_ERR"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"
}

echo -e "${CYAN}Building ViperIDE${NC}"
echo "Source: $IDE_DIR"
echo "Output: $OUTPUT_FILE"
echo "=============================================="

case "$(uname -s 2>/dev/null)" in
    Darwin)
        build_macos
        ;;
    Linux)
        build_linux
        ;;
    *)
        echo -e "${RED}Error: unsupported platform${NC}"
        exit 1
        ;;
esac

size=$(ls -lh "$OUTPUT_FILE" | awk '{print $5}')
echo -e "${GREEN}Built: $OUTPUT_FILE ($size)${NC}"
