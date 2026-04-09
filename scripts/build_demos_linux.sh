#!/bin/bash
# Build and smoke-run demo binaries on Linux using Viper object generation
# plus the system linker.
# Usage: ./scripts/build_demos_linux.sh [--clean] [--skip-run]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"
BIN_DIR="$ROOT_DIR/examples/bin"
GAMES_DIR="$ROOT_DIR/examples/games"
APPS_DIR="$ROOT_DIR/examples/apps"

VIPER="$BUILD_DIR/src/tools/viper/viper"
CC_CMD="${CC:-cc}"
RUN_TIMEOUT_DEFAULT="${VIPER_DEMO_TIMEOUT:-5}"
RUN_DEMO_RC=0

RUNTIME_ARCHIVE="$BUILD_DIR/src/runtime/libviper_runtime.a"
GUI_LIB="$BUILD_DIR/src/lib/gui/libvipergui.a"
GFX_LIB="$BUILD_DIR/lib/libvipergfx.a"
AUDIO_LIB="$BUILD_DIR/lib/libviperaud.a"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

usage() {
    echo "Usage: $0 [--clean] [--skip-run]"
    echo "  --clean      Remove existing binaries before building"
    echo "  --skip-run   Build binaries only; skip launch validation"
    exit 1
}

CLEAN=0
SKIP_RUN=0
for arg in "$@"; do
    case "$arg" in
        --clean)
            CLEAN=1
            ;;
        --skip-run)
            SKIP_RUN=1
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

if [[ "$(uname -s)" != "Linux" ]]; then
    echo -e "${RED}Error: build_demos_linux.sh must be run on Linux${NC}"
    exit 1
fi

case "$(uname -m)" in
    x86_64|amd64)
        TARGET_ARCH="x64"
        ;;
    *)
        echo -e "${RED}Error: build_demos_linux.sh currently supports x86_64 Linux only${NC}"
        exit 1
        ;;
esac

if [[ ! -x "$VIPER" ]]; then
    echo -e "${RED}Error: viper tool not found at $VIPER${NC}"
    echo "Run './scripts/build_viper_linux.sh' first"
    exit 1
fi

if ! command -v "$CC_CMD" >/dev/null 2>&1; then
    echo -e "${RED}Error: C compiler '$CC_CMD' not found${NC}"
    exit 1
fi

if [[ $SKIP_RUN -eq 0 ]] && ! command -v timeout >/dev/null 2>&1; then
    echo -e "${RED}Error: 'timeout' command not found${NC}"
    exit 1
fi

for required in "$RUNTIME_ARCHIVE" "$GUI_LIB" "$GFX_LIB" "$AUDIO_LIB"; do
    if [[ ! -f "$required" ]]; then
        echo -e "${RED}Error: required library not found: $required${NC}"
        echo "Run './scripts/build_viper_linux.sh' first"
        exit 1
    fi
done

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

link_demo() {
    local obj_file="$1"
    local exe_file="$2"
    local err_file="$3"

    if ! "$CC_CMD" \
        "$obj_file" \
        "$RUNTIME_ARCHIVE" \
        "$GUI_LIB" \
        "$GFX_LIB" \
        "$AUDIO_LIB" \
        -lX11 \
        -lasound \
        -pthread \
        -lm \
        -ldl \
        -o "$exe_file" >"$err_file" 2>&1; then
        return 1
    fi

    return 0
}

snapshot_bin_dir() {
    find "$BIN_DIR" -mindepth 1 -maxdepth 1 -printf '%f\n' | LC_ALL=C sort
}

cleanup_run_artifacts() {
    local before_list="$1"
    local after_list="$2"
    local keep_entry="$3"

    while IFS= read -r entry; do
        if [[ "$entry" == "$keep_entry" ]]; then
            continue
        fi
        rm -rf -- "$BIN_DIR/$entry"
    done < <(comm -13 "$before_list" "$after_list")
}

run_demo() {
    local name="$1"
    local exe_file="$2"
    local run_out="$3"
    local run_err="$4"
    local timeout_secs="$RUN_TIMEOUT_DEFAULT"
    RUN_DEMO_RC=0

    if [[ $SKIP_RUN -eq 1 ]]; then
        return 0
    fi

    case "$name" in
        3dbowling|vipersql|xenoscape)
            timeout_secs=10
            ;;
    esac

    local before_list="${run_out}.before"
    local after_list="${run_out}.after"
    snapshot_bin_dir >"$before_list"

    local rc=0
    (
        cd "$BIN_DIR"
        timeout "$timeout_secs" "./$(basename "$exe_file")"
    ) >"$run_out" 2>"$run_err" || rc=$?

    snapshot_bin_dir >"$after_list"
    cleanup_run_artifacts "$before_list" "$after_list" "$(basename "$exe_file")"
    rm -f "$before_list" "$after_list"

    if [[ $rc -eq 0 || $rc -eq 124 ]]; then
        RUN_DEMO_RC=$rc
        return 0
    fi

    RUN_DEMO_RC=$rc
    return 1
}

build_demo() {
    local name="$1"
    local project_dir="$2"
    local exe_file="$BIN_DIR/${name}"
    local tmp_base="/tmp/viper_demo_${name}_$$"
    local il_file="${tmp_base}.il"
    local obj_file="${tmp_base}.o"
    local frontend_err="${tmp_base}.front.err"
    local codegen_out="${tmp_base}.codegen.out"
    local codegen_err="${tmp_base}.codegen.err"
    local link_err="${tmp_base}.link.err"
    local run_out="${tmp_base}.run.out"
    local run_err="${tmp_base}.run.err"

    if [[ ! -f "$project_dir/viper.project" ]]; then
        echo -e "${RED}  Error: No viper.project found in $project_dir${NC}"
        return 1
    fi

    echo -n "  Frontend -> IL... "
    if ! "$VIPER" build "$project_dir" -o "$il_file" 2>"$frontend_err"; then
        echo -e "${RED}FAILED${NC}"
        head -20 "$frontend_err"
        rm -f "$il_file" "$obj_file" "$frontend_err" "$codegen_out" "$codegen_err" \
            "$link_err" "$run_out" "$run_err"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    echo -n "  Codegen (native obj)... "
    if ! "$VIPER" codegen "$TARGET_ARCH" compile "$il_file" --native-asm -O1 -o "$obj_file" \
        >"$codegen_out" 2>"$codegen_err"; then
        echo -e "${RED}FAILED${NC}"
        head -20 "$codegen_err"
        rm -f "$il_file" "$obj_file" "$frontend_err" "$codegen_out" "$codegen_err" \
            "$link_err" "$run_out" "$run_err"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    echo -n "  Link (system cc)... "
    if ! link_demo "$obj_file" "$exe_file" "$link_err"; then
        echo -e "${RED}FAILED${NC}"
        head -40 "$link_err"
        rm -f "$il_file" "$obj_file" "$frontend_err" "$codegen_out" "$codegen_err" \
            "$link_err" "$run_out" "$run_err"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    if [[ $SKIP_RUN -eq 0 ]]; then
        echo -n "  Run smoke... "
        if ! run_demo "$name" "$exe_file" "$run_out" "$run_err"; then
            echo -e "${RED}FAILED${NC}"
            echo "  Exit code: $RUN_DEMO_RC"
            if [[ -s "$run_out" ]]; then
                echo "  stdout:"
                head -20 "$run_out"
            fi
            if [[ -s "$run_err" ]]; then
                echo "  stderr:"
                head -20 "$run_err"
            fi
            rm -f "$il_file" "$obj_file" "$frontend_err" "$codegen_out" "$codegen_err" \
                "$link_err" "$run_out" "$run_err"
            return 1
        fi
        echo -e "${GREEN}OK${NC}"
    fi

    rm -f "$il_file" "$obj_file" "$frontend_err" "$codegen_out" "$codegen_err" \
        "$link_err" "$run_out" "$run_err"

    local size
    size=$(ls -lh "$exe_file" | awk '{print $5}')
    echo -e "  ${GREEN}Built: $exe_file ($size)${NC}"
    return 0
}

echo -e "${CYAN}Building Viper demos on Linux (${TARGET_ARCH})${NC}"
echo -e "${CYAN}Object generation: Viper native backend${NC}"
echo -e "${CYAN}Final link: system cc${NC}"
if [[ $SKIP_RUN -eq 0 ]]; then
    echo -e "${CYAN}Run validation: launch from ./examples/bin with timeout=${RUN_TIMEOUT_DEFAULT}s${NC}"
fi
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
