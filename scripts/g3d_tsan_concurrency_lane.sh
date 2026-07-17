#!/usr/bin/env bash
# Script: g3d_tsan_concurrency_lane.sh
# Purpose: Run the focused 3D Next Level concurrency race lane under TSan.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/cmake-build-tsan-g3d}"
JOBS="${JOBS:-8}"
CTEST_JOBS="${CTEST_JOBS:-1}"

CONFIGURE=true
for arg in "$@"; do
    case "$arg" in
        --no-configure)
            CONFIGURE=false
            ;;
        --help|-h)
            cat <<'USAGE'
Usage: scripts/g3d_tsan_concurrency_lane.sh [--no-configure]

Environment:
  BUILD_DIR    TSan build directory (default: <repo>/cmake-build-tsan-g3d)
  JOBS         Build parallelism (default: 8)
  CTEST_JOBS   CTest parallelism (default: 1)
USAGE
            exit 0
            ;;
        *)
            echo "Unknown argument: ${arg}" >&2
            exit 2
            ;;
    esac
done

if ! command -v clang >/dev/null 2>&1 || ! command -v clang++ >/dev/null 2>&1; then
    echo "clang and clang++ are required for the TSan concurrency lane." >&2
    exit 1
fi

TARGETS=(
    zanna
    test_rt_threadpool
    test_rt_parallel
    test_rt_parallel_reduce
    test_rt_concurrency
    test_rt_g3d_commit_queue
    test_rt_gltf
    test_rt_game3d
    test_rt_game3d_streaming_async
    g3d_3dnext2_surface_probe
)

TEST_REGEX='^(test_rt_threadpool|test_rt_parallel|test_rt_parallel_reduce|test_rt_concurrency|test_rt_g3d_commit_queue|test_rt_gltf|test_rt_game3d|test_rt_game3d_streaming_async|g3d_3dnext2_surface_probe|g3d_openworld_slice_streaming_hitch_probe)$'

if ${CONFIGURE}; then
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DIL_SANITIZE_THREAD=ON \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++
fi

cmake --build "${BUILD_DIR}" --parallel "${JOBS}" --target "${TARGETS[@]}"

ctest --test-dir "${BUILD_DIR}" \
    -R "${TEST_REGEX}" \
    --output-on-failure \
    --timeout 120 \
    -j "${CTEST_JOBS}"
