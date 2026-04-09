#!/bin/bash
# Build native binaries for all demos using the native assembler and linker.
# This uses zero external tools — no system assembler (cc -c), no system linker (cc/ld).
# Usage: ./scripts/build_demos_asmlnk.sh [--clean]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
BIN_DIR="$ROOT_DIR/examples/bin"
GAMES_DIR="$ROOT_DIR/examples/games"
APPS_DIR="$ROOT_DIR/examples/apps"

VIPER="$BUILD_DIR/src/tools/viper/viper"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

usage() {
    echo "Usage: $0 [--clean]"
    echo "  --clean    Remove existing binaries before building"
    exit 1
}

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

if [[ ! -x "$VIPER" ]]; then
    echo -e "${RED}Error: viper tool not found at $VIPER${NC}"
    echo "Run './scripts/build_viper_mac.sh' first"
    exit 1
fi

mkdir -p "$BIN_DIR"

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning existing binaries..."
    rm -f "$BIN_DIR"/*
fi

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
    "vipersql:${APPS_DIR}/vipersql"
    "chess-zia:${GAMES_DIR}/chess"
    "xenoscape:${GAMES_DIR}/xenoscape"
)

build_demo() {
    local name="$1"
    local project_dir="$2"
    local exe_file="$BIN_DIR/${name}"
    local il_file="/tmp/viper_native_build_${name}_$$.il"

    if [[ ! -f "$project_dir/viper.project" ]]; then
        echo -e "${RED}  Error: No viper.project found in $project_dir${NC}"
        return 1
    fi

    # Step 1: Frontend — compile to IL.
    echo -n "  Frontend -> IL... "
    if ! "$VIPER" build "$project_dir" -o "$il_file" 2>/tmp/viper_nativebuild_err_$$; then
        echo -e "${RED}FAILED${NC}"
        head -20 /tmp/viper_nativebuild_err_$$
        rm -f /tmp/viper_nativebuild_err_$$ "$il_file"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    # Step 2: Native codegen — binary encoder + native linker.
    echo -n "  Codegen (native asm+link)... "
    if ! "$VIPER" codegen arm64 "$il_file" --native-asm --native-link -O1 -o "$exe_file" 2>/tmp/viper_nativebuild_err_$$; then
        echo -e "${RED}FAILED${NC}"
        head -20 /tmp/viper_nativebuild_err_$$
        rm -f /tmp/viper_nativebuild_err_$$ "$il_file"
        return 1
    fi
    rm -f /tmp/viper_nativebuild_err_$$ "$il_file"
    echo -e "${GREEN}OK${NC}"

    local size=$(ls -lh "$exe_file" | awk '{print $5}')
    echo -e "  ${GREEN}Built: $exe_file ($size)${NC}"
    return 0
}

echo -e "${CYAN}Building Viper demos with native assembler + linker (arm64)${NC}"
echo -e "${CYAN}Zero external tools — no cc, no ld, no codesign${NC}"
echo "=============================================="
echo ""

FAILED=0
SUCCEEDED=0
SKIPPED=0

echo "=== BASIC Demos ==="
echo ""

for demo in "${BASIC_DEMOS[@]}"; do
    IFS=':' read -r name project_dir <<< "$demo"
    if [[ ! -d "$project_dir" ]]; then
        echo -e "Skipping $name (${YELLOW}directory not found${NC})"
        SKIPPED=$((SKIPPED + 1))
        echo ""
        continue
    fi
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
    if [[ ! -d "$project_dir" ]]; then
        echo -e "Skipping $name (${YELLOW}directory not found${NC})"
        SKIPPED=$((SKIPPED + 1))
        echo ""
        continue
    fi
    echo "Building $name..."
    if build_demo "$name" "$project_dir"; then
        SUCCEEDED=$((SUCCEEDED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
    echo ""
done

echo "=============================================="
TOTAL=$((SUCCEEDED + FAILED + SKIPPED))
echo -e "Results: ${GREEN}$SUCCEEDED passed${NC}, ${RED}$FAILED failed${NC}, ${YELLOW}$SKIPPED skipped${NC} (of $TOTAL)"

if [[ $FAILED -eq 0 ]]; then
    echo ""
    echo "Binaries are in: $BIN_DIR"
    ls -lh "$BIN_DIR"
else
    exit 1
fi
