#!/usr/bin/env bash
# Script: update_goldens.sh
# Purpose: Re-run golden tests with UPDATE_GOLDEN=1 to overwrite expected
#          output files with actual output. Use after intentional changes
#          to compiler diagnostics, IL output, or optimizer behavior.
#
# Usage:
#   ./scripts/update_goldens.sh              # Update all golden files
#   ./scripts/update_goldens.sh il_opt       # Update only IL optimizer goldens
#   ./scripts/update_goldens.sh basic_error  # Update only BASIC error goldens
#
# The script runs each failing golden test with -DUPDATE_GOLDEN=1 passed
# through to the cmake runner scripts. Tests that already pass are skipped.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build}"

if [ ! -d "${BUILD_DIR}" ]; then
    echo "Error: Build directory not found at ${BUILD_DIR}"
    echo "Run the appropriate build script first (for example ./scripts/build_viper_linux.sh or ./scripts/build_viper_mac.sh)."
    exit 1
fi

FILTER="${1:-}"

# Collect golden test names
if [ -n "${FILTER}" ]; then
    TESTS=$(ctest --test-dir "${BUILD_DIR}" -N -L golden 2>/dev/null \
        | grep "Test #" | sed 's/.*Test #[0-9]*: //' \
        | grep "${FILTER}" || true)
else
    TESTS=$(ctest --test-dir "${BUILD_DIR}" -N -L golden 2>/dev/null \
        | grep "Test #" | sed 's/.*Test #[0-9]*: //' || true)
fi

if [ -z "${TESTS}" ]; then
    echo "No golden tests found${FILTER:+ matching '${FILTER}'}."
    exit 0
fi

TOTAL=$(echo "${TESTS}" | wc -l | tr -d ' ')
UPDATED=0
SKIPPED=0
FAILED=0

echo "Checking ${TOTAL} golden tests${FILTER:+ matching '${FILTER}'}..."
echo ""

for test_name in ${TESTS}; do
    # First, run normally to see if it passes
    if ctest --test-dir "${BUILD_DIR}" -R "^${test_name}$" --timeout 30 > /dev/null 2>&1; then
        SKIPPED=$((SKIPPED + 1))
        continue
    fi

    # Test failed — re-run with UPDATE_GOLDEN
    # We need to find the test command and inject -DUPDATE_GOLDEN=1
    # CTest doesn't support passing extra args, so we extract and re-run
    TEST_CMD=$(ctest --test-dir "${BUILD_DIR}" -N -V -R "^${test_name}$" 2>/dev/null \
        | grep "Test command:" | sed 's/.*Test command: //')

    if [ -z "${TEST_CMD}" ]; then
        echo "  SKIP  ${test_name} (could not extract command)"
        FAILED=$((FAILED + 1))
        continue
    fi

    # Inject -DUPDATE_GOLDEN=1 before the -P flag
    UPDATED_CMD=$(echo "${TEST_CMD}" | sed 's/-P /-DUPDATE_GOLDEN=1 -P /')

    if eval "${UPDATED_CMD}" > /dev/null 2>&1; then
        echo "  UPDATE  ${test_name}"
        UPDATED=$((UPDATED + 1))
    else
        echo "  FAIL    ${test_name} (update command failed)"
        FAILED=$((FAILED + 1))
    fi
done

echo ""
echo "Done: ${UPDATED} updated, ${SKIPPED} already passing, ${FAILED} failed."
