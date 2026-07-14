#!/bin/sh
#===----------------------------------------------------------------------===#
#
# Part of the Viper project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: examples/games/ridgebound/run_probes.sh
# Purpose: Run every Ridgebound release gate with an existing Viper binary.
# Key invariants:
#   - The script never configures, builds, or invokes CTest.
#   - A probe passes only after a clean exit and a RESULT: ok line.
# Ownership/Lifetime:
#   - Temporary output is removed on exit and interruption.
# Links: run_probes.cmd, IMPROVEMENT_AUDIT.md
#
#===----------------------------------------------------------------------===#

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
VIPER_BIN=${VIPER_BIN:-viper}
OUTPUT=$(mktemp "${TMPDIR:-/tmp}/ridgebound-probe.XXXXXX") || exit 1
trap 'rm -f "$OUTPUT"' EXIT HUP INT TERM

if ! command -v "$VIPER_BIN" >/dev/null 2>&1; then
    echo "Ridgebound probes: Viper binary not found: $VIPER_BIN" >&2
    echo "Set VIPER_BIN to an existing executable; this runner never builds it." >&2
    exit 1
fi

cd "$REPO_ROOT" || exit 1
if ! "$VIPER_BIN" check "$SCRIPT_DIR" --diagnostic-format=json >"$OUTPUT" 2>&1; then
    cat "$OUTPUT"
    echo "Ridgebound probes: project check failed" >&2
    exit 1
fi

passed=0
failed=0
for probe in topology_probe traversal_probe state_probe smoke_probe; do
    echo "==> $probe"
    if "$VIPER_BIN" run "$SCRIPT_DIR/$probe.zia" >"$OUTPUT" 2>&1; then
        status=0
    else
        status=$?
    fi
    cat "$OUTPUT"
    if [ "$status" -eq 0 ] && grep -Fq "RESULT: ok" "$OUTPUT"; then
        passed=$((passed + 1))
    else
        echo "PROBE FAILED: $probe (exit $status)" >&2
        failed=$((failed + 1))
    fi
done

echo "Ridgebound probes: $passed passed, $failed failed"
if [ "$failed" -ne 0 ]; then
    exit 1
fi
