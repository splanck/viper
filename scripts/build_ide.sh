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
COMPAT_OUTPUT_FILE="${VIPER_IDE_COMPAT_OUTPUT:-$BUILD_DIR/viperide/viperide}"
SKIP_COMPAT_COPY="${VIPER_IDE_SKIP_COMPAT_COPY:-0}"
VIPER_BUILD_TYPE="${VIPER_BUILD_TYPE:-Debug}"
VIPER=""
VIPER_IS_WINDOWS=0

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

usage() {
    echo "Usage: $0 [--clean] [--output PATH]"
    echo "  --clean        Remove the existing ViperIDE binary before building"
    echo "  --output PATH  Write the binary to PATH (default: viperide/bin/viperide)"
    echo "  Compatibility copy: build/viperide/viperide unless VIPER_IDE_SKIP_COMPAT_COPY=1"
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

resolve_viper_tool() {
    local candidate
    local candidates=(
        "$BUILD_DIR/src/tools/viper/viper"
        "$BUILD_DIR/src/tools/viper/viper.exe"
        "$BUILD_DIR/src/tools/viper/$VIPER_BUILD_TYPE/viper.exe"
        "$BUILD_DIR/src/tools/viper/Debug/viper.exe"
        "$BUILD_DIR/src/tools/viper/Release/viper.exe"
    )

    for candidate in "${candidates[@]}"; do
        if [[ -x "$candidate" ]]; then
            VIPER="$candidate"
            if [[ "$candidate" == *.exe ]]; then
                VIPER_IS_WINDOWS=1
            fi
            return 0
        fi
    done
    return 1
}

windows_path() {
    local path="$1"
    if command -v wslpath >/dev/null 2>&1; then
        wslpath -w "$path"
        return 0
    fi
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -w "$path"
        return 0
    fi
    printf '%s\n' "$path"
}

path_for_viper() {
    if [[ $VIPER_IS_WINDOWS -eq 1 ]]; then
        windows_path "$1"
    else
        printf '%s\n' "$1"
    fi
}

if ! resolve_viper_tool; then
    echo -e "${RED}Error: viper tool not found under $BUILD_DIR/src/tools/viper${NC}"
    echo "Run './scripts/build_viper_mac.sh', './scripts/build_viper_linux.sh', or './scripts/build_viper_win.cmd' first"
    exit 1
fi

if [[ $VIPER_IS_WINDOWS -eq 1 ]]; then
    if [[ -z "${VIPER_IDE_OUTPUT:-}" && "$OUTPUT_FILE" == "$OUT_DIR/viperide" ]]; then
        OUTPUT_FILE="$OUT_DIR/viperide.exe"
    fi
    if [[ -z "${VIPER_IDE_COMPAT_OUTPUT:-}" && "$COMPAT_OUTPUT_FILE" == "$BUILD_DIR/viperide/viperide" ]]; then
        COMPAT_OUTPUT_FILE="$BUILD_DIR/viperide/viperide.exe"
    fi
fi

if [[ ! -x "$VIPER" ]]; then
    echo -e "${RED}Error: viper tool not found at $VIPER${NC}"
    echo "Run './scripts/build_viper_mac.sh' or './scripts/build_viper_linux.sh' first"
    exit 1
fi

mkdir -p "$(dirname "$OUTPUT_FILE")"

if [[ $CLEAN -eq 1 ]]; then
    rm -f "$OUTPUT_FILE"
    rm -f "$(dirname "$OUTPUT_FILE")/viperide.buildinfo"
    if [[ "$OUTPUT_FILE" != "$COMPAT_OUTPUT_FILE" ]]; then
        rm -f "$COMPAT_OUTPUT_FILE"
        rm -f "$(dirname "$COMPAT_OUTPUT_FILE")/viperide.buildinfo"
    fi
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
    local ide_arg output_arg build_dir_arg
    ide_arg="$(path_for_viper "$IDE_DIR")"
    output_arg="$(path_for_viper "$OUTPUT_FILE")"
    build_dir_arg="$(path_for_viper "$BUILD_DIR")"
    echo -n "  Viper build (--arch $target_arch)... "
    if ! VIPER_BUILD_DIR="$build_dir_arg" "$VIPER" build "$ide_arg" --arch "$target_arch" -o "$output_arg" 2>"$FRONTEND_ERR"; then
        echo -e "${RED}FAILED${NC}"
        head -40 "$FRONTEND_ERR"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"
}

build_info_text() {
    local binary_path="$1"
    local timestamp
    timestamp="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    local revision
    if revision="$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null)"; then
        :
    else
        revision="unknown"
    fi
    local dirty=""
    if git -C "$ROOT_DIR" diff --quiet --ignore-submodules -- 2>/dev/null; then
        dirty=""
    else
        dirty=" dirty"
    fi
    printf 'Build: %s\nSource: %s%s\nOutput: %s\nViper: %s\n' \
        "$timestamp" "$revision" "$dirty" "$binary_path" "$VIPER"
}

write_build_info() {
    local binary_path="$1"
    local info_path
    info_path="$(dirname "$binary_path")/viperide.buildinfo"
    mkdir -p "$(dirname "$info_path")"
    build_info_text "$binary_path" >"$info_path"
}

mirror_compat_output() {
    if [[ "$SKIP_COMPAT_COPY" == "1" ]]; then
        return 0
    fi
    if [[ "$OUTPUT_FILE" == "$COMPAT_OUTPUT_FILE" ]]; then
        return 0
    fi
    mkdir -p "$(dirname "$COMPAT_OUTPUT_FILE")"
    cp -p "$OUTPUT_FILE" "$COMPAT_OUTPUT_FILE"
    write_build_info "$COMPAT_OUTPUT_FILE"
    local compat_size
    compat_size=$(ls -lh "$COMPAT_OUTPUT_FILE" | awk '{print $5}')
    echo -e "${GREEN}Compatibility copy: $COMPAT_OUTPUT_FILE ($compat_size)${NC}"
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
write_build_info "$OUTPUT_FILE"
mirror_compat_output
echo -e "${GREEN}Built: $OUTPUT_FILE ($size)${NC}"
echo "Build info: $(dirname "$OUTPUT_FILE")/viperide.buildinfo"
