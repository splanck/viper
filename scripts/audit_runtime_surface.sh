#!/usr/bin/env bash
# Audit drift between the C runtime surface and frontend-visible Viper APIs.
set -euo pipefail

BUILD_DIR="build"
STRICT_FLAG=""
STRICT_HEADER_FLAG=""
SUMMARY_FLAG=""

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
        *)
            echo "usage: $0 [--strict-header-sync] [--strict-unclassified] [--summary-only] [--build-dir=build]" >&2
            exit 1
            ;;
    esac
done

if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"
else
    JOBS="${NUMBER_OF_PROCESSORS:-8}"
fi

make -C "${BUILD_DIR}" -j"${JOBS}" \
    rtgen \
    test_runtime_surface_audit \
    test_runtime_name_map \
    test_runtime_classes_catalog \
    test_zia_static_calls \
    test_basic_runtime_calls

audit_cmd=("${BUILD_DIR}/src/rtgen" --audit)
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
"${audit_cmd[@]}"

ctest --test-dir "${BUILD_DIR}" --output-on-failure -R \
    '^(test_runtime_surface_audit|test_runtime_name_map|test_runtime_classes_catalog|test_zia_static_calls|test_basic_runtime_calls)$'
