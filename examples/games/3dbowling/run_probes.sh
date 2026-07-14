#!/bin/sh
#===----------------------------------------------------------------------===#
#
# Part of the Viper project, under the GNU GPL v3.
# See LICENSE for license information.
#
#===----------------------------------------------------------------------===#
#
# File: examples/games/3dbowling/run_probes.sh
# Purpose: Run every 3dbowling release gate with an existing Viper binary.
# Key invariants:
#   - The script never configures, builds, or invokes CTest.
#   - A probe passes only when it exits cleanly and prints exactly RESULT: ok.
# Ownership/Lifetime:
#   - Temporary output is removed on normal exit and interruption.
# Links: run_probes.cmd, LONG_TERM_IMPROVEMENT_SPEC.md
#
#===----------------------------------------------------------------------===#

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
VIPER_BIN=${VIPER_BIN:-viper}
OUTPUT=$(mktemp "${TMPDIR:-/tmp}/3dbowling-probe.XXXXXX") || exit 1
trap 'rm -f "$OUTPUT"' EXIT HUP INT TERM

PROBES="
release_upgrade_probe
pinfall_contract_probe
impact_order_probe
oil_grid_probe
trajectory_probe
ai_delivery_probe
feedback_probe
stability_probe
replay_scene_probe
lifecycle_probe
asset_resolution_probe
save_clamp_probe
frame_rate_probe
layout_probe
accessibility_probe
menu_flow_probe
match_mode_probe
asset_probe
asset_render_probe
aim_smoke_probe
overlay_smoke_probe
smoke_probe
title_nopostfx_smoke
title_postfx_smoke
scene_nopostfx_smoke
release_visual_probe
release_menu_probe
"

if ! command -v "$VIPER_BIN" >/dev/null 2>&1; then
    echo "3dbowling probes: Viper binary not found: $VIPER_BIN" >&2
    echo "Set VIPER_BIN to an existing viper executable; this runner never builds it." >&2
    exit 1
fi

cd "$REPO_ROOT" || exit 1
passed=0
failed=0
for probe in $PROBES; do
    echo "==> $probe"
    if "$VIPER_BIN" run "$SCRIPT_DIR/$probe.zia" >"$OUTPUT" 2>&1; then
        status=0
    else
        status=$?
    fi
    cat "$OUTPUT"
    if [ "$status" -eq 0 ] && grep -Fqx "RESULT: ok" "$OUTPUT"; then
        passed=$((passed + 1))
    else
        echo "PROBE FAILED: $probe (exit $status)" >&2
        failed=$((failed + 1))
    fi
done

echo "3dbowling probes: $passed passed, $failed failed"
if [ "$failed" -ne 0 ]; then
    exit 1
fi
