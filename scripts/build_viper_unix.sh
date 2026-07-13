#!/usr/bin/env bash
# build_viper_unix.sh — canonical POSIX Viper build + test + install script.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
PLATFORM="$(uname -s 2>/dev/null || true)"

detect_build_jobs() {
    case "$PLATFORM" in
        Darwin)
            sysctl -n hw.ncpu 2>/dev/null || echo 8
            ;;
        Linux)
            if command -v nproc >/dev/null 2>&1; then
                nproc
            else
                getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8
            fi
            ;;
    esac
}

detect_macos_performance_cores() {
    local perf_cores
    perf_cores="$(sysctl -n hw.perflevel0.physicalcpu 2>/dev/null || true)"
    if [[ -n "$perf_cores" && "$perf_cores" =~ ^[0-9]+$ && "$perf_cores" -gt 0 ]]; then
        echo "$perf_cores"
        return
    fi
    sysctl -n hw.physicalcpu 2>/dev/null || detect_build_jobs
}

case "$PLATFORM" in
    Darwin)
        DEFAULT_BUILD_TYPE="Debug"
        DEFAULT_CTEST_JOBS="$(detect_macos_performance_cores)"
        DEFAULT_FAST_DEBUG="1"
        ;;
    Linux)
        DEFAULT_BUILD_TYPE="Debug"
        DEFAULT_CTEST_JOBS="$(detect_build_jobs)"
        DEFAULT_FAST_DEBUG="1"
        ;;
    *)
        echo "Error: build_viper_unix.sh supports macOS and Linux only"
        exit 1
        ;;
esac

JOBS="${VIPER_JOBS:-$(detect_build_jobs)}"
CTEST_JOBS="${VIPER_CTEST_JOBS:-$DEFAULT_CTEST_JOBS}"
BUILD_DIR="${VIPER_BUILD_DIR:-$ROOT_DIR/build}"
BUILD_TYPE="${VIPER_BUILD_TYPE:-$DEFAULT_BUILD_TYPE}"
FAST_DEBUG="${VIPER_FAST_DEBUG:-$DEFAULT_FAST_DEBUG}"
SKIP_INSTALL="${VIPER_SKIP_INSTALL:-0}"
SKIP_AUDIT="${VIPER_SKIP_AUDIT:-0}"
SKIP_LINT="${VIPER_SKIP_LINT:-0}"
LINT_CHANGED_ONLY="${VIPER_LINT_CHANGED_ONLY:-1}"
SKIP_SMOKE="${VIPER_SKIP_SMOKE:-0}"
SKIP_TESTS="${VIPER_SKIP_TESTS:-0}"
SKIP_CLEAN="${VIPER_SKIP_CLEAN:-0}"
TEST_LABEL="${VIPER_TEST_LABEL:-}"
NO_CCACHE="${VIPER_NO_CCACHE:-0}"
RUN_SLOW_TESTS="${VIPER_RUN_SLOW_TESTS:-0}"
CTEST_TIMEOUT="${VIPER_CTEST_TIMEOUT:-}"
INSTALL_PREFIX="${VIPER_INSTALL_PREFIX:-/usr/local}"
INSTALL_PREFIX_EXPLICIT=0
if [[ -n "${VIPER_INSTALL_PREFIX+x}" ]]; then
    INSTALL_PREFIX_EXPLICIT=1
fi
CONFIGURE_ARGS=(
    -S "$ROOT_DIR"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DVIPER_FAST_DEBUG="$FAST_DEBUG"
)

if [[ -n "${VIPER_CMAKE_GENERATOR:-}" ]]; then
    CONFIGURE_ARGS+=(-G "${VIPER_CMAKE_GENERATOR}")
fi

if command -v clang >/dev/null 2>&1; then
    CONFIGURE_ARGS+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
elif command -v gcc >/dev/null 2>&1; then
    CONFIGURE_ARGS+=(-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++)
fi

# Compiler cache: speeds up rebuilds after clean or branch switches.
# Auto-detected; set VIPER_NO_CCACHE=1 to disable.
if [[ "$NO_CCACHE" != "1" ]] && command -v ccache >/dev/null 2>&1; then
    echo "[build_viper] ccache detected; enabling compiler launcher (set VIPER_NO_CCACHE=1 to disable)"
    CONFIGURE_ARGS+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

if [[ -n "${VIPER_EXTRA_CMAKE_ARGS:-}" ]]; then
    # shellcheck disable=SC2206
    EXTRA_ARGS=( ${VIPER_EXTRA_CMAKE_ARGS} )
    CONFIGURE_ARGS+=("${EXTRA_ARGS[@]}")
fi

if [[ "$SKIP_CLEAN" != "1" ]]; then
    cmake --build "$BUILD_DIR" --target clean-all 2>/dev/null || true
else
    echo "[build_viper] Skipping clean (VIPER_SKIP_CLEAN=1); incremental rebuild"
fi
cmake "${CONFIGURE_ARGS[@]}"
echo "[build_viper] Build type: $BUILD_TYPE"
echo "[build_viper] Fast Debug: $FAST_DEBUG"
echo "[build_viper] Build jobs: $JOBS"
cmake --build "$BUILD_DIR" -j"$JOBS"

if command -v sync >/dev/null 2>&1; then
    sync
    sleep 1
else
    cmake -E sleep 1
fi

if [[ "$SKIP_TESTS" == "1" ]]; then
    echo "[build_viper] Skipping tests (VIPER_SKIP_TESTS=1)"
else
    rm -rf "$BUILD_DIR/Testing"
    if [[ -n "$TEST_LABEL" ]]; then
        echo "[build_viper] Running tests labeled '$TEST_LABEL' (VIPER_TEST_LABEL)..."
    else
        echo "[build_viper] Running full test suite..."
    fi
    echo "[build_viper] CTest jobs: $CTEST_JOBS"
    CTEST_ARGS=(--test-dir "$BUILD_DIR" --output-on-failure -j"$CTEST_JOBS")
    if [[ -n "$TEST_LABEL" ]]; then
        CTEST_ARGS+=(-L "$TEST_LABEL")
    fi
    if [[ -n "$CTEST_TIMEOUT" ]]; then
        CTEST_ARGS+=(--timeout "$CTEST_TIMEOUT")
    fi
    if [[ "$RUN_SLOW_TESTS" != "1" ]]; then
        echo "[build_viper] Skipping tests labeled slow (set VIPER_RUN_SLOW_TESTS=1 to include them)"
        CTEST_ARGS+=(-LE slow)
    fi
    ctest "${CTEST_ARGS[@]}"
    echo "[build_viper] Test run complete"
fi

if [[ "$SKIP_LINT" != "1" && -x "$SCRIPT_DIR/lint_platform_policy.sh" ]]; then
    echo "[build_viper] Running platform policy lint..."
    if [[ "$LINT_CHANGED_ONLY" == "1" ]]; then
        "$SCRIPT_DIR/lint_platform_policy.sh" --changed-only
    else
        "$SCRIPT_DIR/lint_platform_policy.sh"
    fi
fi

if [[ "$SKIP_AUDIT" != "1" && -x "$SCRIPT_DIR/audit_runtime_surface.sh" ]]; then
    echo "[build_viper] Running runtime surface audit..."
    "$SCRIPT_DIR/audit_runtime_surface.sh" --build-dir="$BUILD_DIR"
fi

if [[ "$SKIP_SMOKE" != "1" && -x "$SCRIPT_DIR/run_cross_platform_smoke.sh" ]]; then
    echo "[build_viper] Running cross-platform smoke tests..."
    "$SCRIPT_DIR/run_cross_platform_smoke.sh" --build-dir "$BUILD_DIR"
fi

if [[ "$SKIP_INSTALL" == "1" ]]; then
    echo "Skipping install (VIPER_SKIP_INSTALL=1)"
    exit 0
fi

echo "[build_viper] Installing to $INSTALL_PREFIX..."
if [[ "$(id -u)" -eq 0 ]]; then
    cmake --install "$BUILD_DIR" --prefix "$INSTALL_PREFIX"
else
    if { [[ -d "$INSTALL_PREFIX" ]] || mkdir -p "$INSTALL_PREFIX" 2>/dev/null; } && [[ -w "$INSTALL_PREFIX" ]]; then
        cmake --install "$BUILD_DIR" --prefix "$INSTALL_PREFIX"
        echo "[build_viper] Install complete"
        exit 0
    fi
    if [[ ! -t 0 ]]; then
        if [[ "$INSTALL_PREFIX_EXPLICIT" != "1" && "$INSTALL_PREFIX" == "/usr/local" ]]; then
            INSTALL_PREFIX="$BUILD_DIR/install"
            echo "[build_viper] Non-interactive install: /usr/local requires sudo; using $INSTALL_PREFIX"
            cmake --install "$BUILD_DIR" --prefix "$INSTALL_PREFIX"
            echo "[build_viper] Install complete"
            exit 0
        fi
        echo "error: install to $INSTALL_PREFIX requires sudo, but stdin is not a terminal" >&2
        echo "Set VIPER_SKIP_INSTALL=1 to build and test only, or set VIPER_INSTALL_PREFIX to a writable prefix." >&2
        exit 1
    fi
    echo "[build_viper] sudo credentials are required for install"
    sudo -v
    sudo cmake --install "$BUILD_DIR" --prefix "$INSTALL_PREFIX"
fi
echo "[build_viper] Install complete"
