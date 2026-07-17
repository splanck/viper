#!/bin/bash
# update_generated_docs.sh
# Regenerates docs/generated/runtime/ from src/il/runtime/runtime.def via the
# rtgen tool (CMake target: generate_runtime_reference).
#
# The generated pages are the canonical runtime API reference; never edit them
# by hand. Authored prose comes from the @summary/@details fragments in
# src/il/runtime/defs/ — fix those and rerun this script.
#
# Usage: ./scripts/update_generated_docs.sh [BUILD_DIR]
#   BUILD_DIR defaults to ./build (must already be configured; run
#   ./scripts/build_zanna_unix.sh first if it is not).

set -euo pipefail

BUILD_DIR="${1:-build}"

if [ ! -d docs/generated/runtime ]; then
    echo "ERROR: docs/generated/runtime not found. Run from the project root." >&2
    exit 2
fi

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "ERROR: $BUILD_DIR is not a configured build directory." >&2
    echo "Run ./scripts/build_zanna_unix.sh (or the platform build script) first." >&2
    exit 2
fi

cmake --build "$BUILD_DIR" --target generate_runtime_reference

echo ""
echo "Regenerated docs/generated/runtime/. Review with: git diff docs/generated/runtime"
