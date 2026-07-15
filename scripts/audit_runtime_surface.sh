#!/usr/bin/env bash
# Audit drift between the C runtime surface and frontend-visible Viper APIs.
set -euo pipefail

BUILD_DIR="build"
STRICT_FLAG=""
STRICT_HEADER_FLAG=""
SUMMARY_FLAG=""
CONFIG="${VIPER_BUILD_TYPE:-Debug}"

for arg in "$@"; do
    case "$arg" in
        --strict-header-sync)
            STRICT_HEADER_FLAG="--strict-header-sync"
            ;;
        --strict-unclassified)
            STRICT_FLAG="--strict-unclassified"
            ;;
        --summary-only)
            SUMMARY_FLAG="--summary-only"
            ;;
        --build-dir=*)
            BUILD_DIR="${arg#--build-dir=}"
            ;;
        --config=*)
            CONFIG="${arg#--config=}"
            ;;
        *)
            echo "usage: $0 [--strict-header-sync] [--strict-unclassified] [--summary-only] [--build-dir=build] [--config=Debug]" >&2
            exit 1
            ;;
    esac
done

normalize_build_dir_for_shell() {
    local path="$1"
    case "$path" in
        [A-Za-z]:\\*|[A-Za-z]:/*)
            if command -v wslpath >/dev/null 2>&1 &&
               grep -qi microsoft /proc/version 2>/dev/null; then
                wslpath -u "$path"
                return
            fi
            if command -v cygpath >/dev/null 2>&1; then
                cygpath -u "$path"
                return
            fi
            ;;
    esac
    printf '%s\n' "$path"
}

BUILD_DIR="$(normalize_build_dir_for_shell "$BUILD_DIR")"

if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"
else
    JOBS="${NUMBER_OF_PROCESSORS:-8}"
fi

echo "[audit] Building runtime surface audit bundle..."
BUILD_DIR_FOR_TOOLS="${BUILD_DIR}"
CMAKE_CMD="cmake"
CTEST_CMD="ctest"
CTEST_CONFIG_ARGS=()
if command -v wslpath >/dev/null 2>&1 && grep -qi microsoft /proc/version 2>/dev/null; then
    if command -v cmake.exe >/dev/null 2>&1; then
        CMAKE_CMD="cmake.exe"
    fi
    if command -v ctest.exe >/dev/null 2>&1; then
        CTEST_CMD="ctest.exe"
    fi
    BUILD_DIR_FOR_TOOLS="$(wslpath -w "${BUILD_DIR}")"
fi
if [[ -n "${CONFIG}" ]]; then
    CTEST_CONFIG_ARGS=(-C "${CONFIG}")
fi

"${CMAKE_CMD}" --build "${BUILD_DIR_FOR_TOOLS}" --config "${CONFIG}" -j"${JOBS}" --target runtime_surface_audit_bundle

RTGEN_EXE="${BUILD_DIR}/src/rtgen"
if [[ ! -x "${RTGEN_EXE}" && -x "${BUILD_DIR}/src/${CONFIG}/rtgen.exe" ]]; then
    RTGEN_EXE="${BUILD_DIR}/src/${CONFIG}/rtgen.exe"
fi
audit_cmd=("${RTGEN_EXE}" --audit)
if [[ -n "${STRICT_HEADER_FLAG}" ]]; then
    audit_cmd+=("${STRICT_HEADER_FLAG}")
fi
if [[ -n "${STRICT_FLAG}" ]]; then
    audit_cmd+=("${STRICT_FLAG}")
fi
if [[ -n "${SUMMARY_FLAG}" ]]; then
    audit_cmd+=("${SUMMARY_FLAG}")
fi
audit_cmd+=(src/il/runtime/runtime.def)
echo "[audit] Running rtgen surface audit..."
"${audit_cmd[@]}"

echo "[audit] Running focused runtime surface tests..."
"${CTEST_CMD}" --test-dir "${BUILD_DIR_FOR_TOOLS}" "${CTEST_CONFIG_ARGS[@]}" --output-on-failure -R \
    '^(test_runtime_surface_audit|test_runtime_name_map|test_runtime_classes_catalog|test_zia_static_calls|test_zia_pointer_safety|test_basic_runtime_calls|test_rt_graphics_surface_link|test_rt_audio_surface_link)$'
