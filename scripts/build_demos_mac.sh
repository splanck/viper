#!/bin/bash
# Build native demo binaries on macOS arm64 using the native assembler and
# linker. Optional smoke-run validation can be enabled with --run. This uses
# zero external tools for the build path — no system assembler (cc -c), no
# system linker (cc/ld).
# Usage: ./scripts/build_demos_mac.sh [--clean] [--run|--skip-run]

set -euo pipefail

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
    echo "Usage: $0 [--clean] [--run|--skip-run]"
    echo "  --clean      Remove existing binaries before building"
    echo "  --run        Launch each built demo for smoke validation"
    echo "  --skip-run   Build only; skip launch validation (default)"
    exit 1
}

CLEAN=0
SKIP_RUN=1
for arg in "$@"; do
    case $arg in
        --clean)
            CLEAN=1
            ;;
        --run)
            SKIP_RUN=0
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

RUN_TIMEOUT_DEFAULT="${VIPER_DEMO_TIMEOUT:-5}"
RUN_DEMO_RC=0

BASIC_DEMOS=(
    "vtris:${GAMES_DIR}/vtris"
    "frogger:${GAMES_DIR}/frogger-basic"
    "centipede:${GAMES_DIR}/centipede-basic"
)

ZIA_DEMOS=(
    "paint:${APPS_DIR}/paint"
    "viperide:${APPS_DIR}/viperide"
    "3dbowling:${GAMES_DIR}/3dbowling"
    "crackman:${GAMES_DIR}/pacman"
    "vipersql:${APPS_DIR}/vipersql"
    "chess-zia:${GAMES_DIR}/chess"
    "xenoscape:${GAMES_DIR}/xenoscape"
)

snapshot_bin_dir() {
    find "$BIN_DIR" -mindepth 1 -maxdepth 1 -print | sed 's#.*/##' | LC_ALL=C sort
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
        3dbowling|vipersql|xenoscape|viperide)
            timeout_secs=10
            ;;
    esac

    local before_list="${run_out}.before"
    local after_list="${run_out}.after"
    snapshot_bin_dir >"$before_list"

    local rc=0
    (
        cd "$BIN_DIR"
        "./$(basename "$exe_file")" >"$run_out" 2>"$run_err" &
        local pid=$!

        sleep "$timeout_secs"
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" >/dev/null 2>&1 || true
            exit 124
        fi

        wait "$pid"
    ) || rc=$?

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
    local frontend_err="${tmp_base}.front.err"
    local codegen_err="${tmp_base}.codegen.err"
    local run_out="${tmp_base}.run.out"
    local run_err="${tmp_base}.run.err"

    if [[ ! -f "$project_dir/viper.project" ]]; then
        echo -e "${RED}  Error: No viper.project found in $project_dir${NC}"
        return 1
    fi

    # Step 1: Frontend — compile to IL.
    echo -n "  Frontend -> IL... "
    if ! "$VIPER" build "$project_dir" -o "$il_file" 2>"$frontend_err"; then
        echo -e "${RED}FAILED${NC}"
        head -20 "$frontend_err"
        rm -f "$frontend_err" "$codegen_err" "$run_out" "$run_err" "$il_file"
        return 1
    fi
    echo -e "${GREEN}OK${NC}"

    # Step 2: Native codegen — binary encoder + native linker.
    echo -n "  Codegen (native asm+link)... "
    if ! "$VIPER" codegen arm64 "$il_file" --native-asm --native-link -O1 -o "$exe_file" 2>"$codegen_err"; then
        echo -e "${RED}FAILED${NC}"
        head -20 "$codegen_err"
        rm -f "$frontend_err" "$codegen_err" "$run_out" "$run_err" "$il_file"
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
            rm -f "$frontend_err" "$codegen_err" "$run_out" "$run_err" "$il_file"
            return 1
        fi
        echo -e "${GREEN}OK${NC}"
    fi

    rm -f "$frontend_err" "$codegen_err" "$run_out" "$run_err" "$il_file"

    local size=$(ls -lh "$exe_file" | awk '{print $5}')
    echo -e "  ${GREEN}Built: $exe_file ($size)${NC}"
    return 0
}

echo -e "${CYAN}Building Viper demos with native assembler + linker (arm64)${NC}"
echo -e "${CYAN}Zero external tools — no cc, no ld, no codesign${NC}"
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
