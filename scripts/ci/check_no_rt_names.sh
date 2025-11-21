#!/usr/bin/env bash
set -euo pipefail

# Guard against reintroducing hardcoded rt_* names in the BASIC frontend.
# Policy: Frontend should resolve runtime helpers via the registry and emit
#         canonical Viper.* names. Use this script in CI to catch regressions.
#
# Behavior:
# - Scans only under src/frontends/basic/
# - Excludes nothing outside that tree (runtime/, tests/, docs/ are allowed)
# - Prints offending lines and returns non-zero when STRICT mode is enabled.
#
# Enable strict mode by setting VIPER_STRICT_NO_RT_NAMES=1 in CI.

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
TARGET_DIR="$ROOT_DIR/src/frontends/basic"

if [[ ! -d "$TARGET_DIR" ]]; then
  echo "error: expected directory not found: $TARGET_DIR" >&2
  exit 2
fi

# Find occurrences of word-boundary rt_ tokens.
# Notes:
# - Keep grep stable output for CI greps.
# - Use LC_ALL=C for consistent sort ordering.
LC_ALL=C
matches=$(rg -n -S --no-heading --glob '!**/*.generated.*' "\\brt_" "$TARGET_DIR" || true)

if [[ -z "$matches" ]]; then
  echo "OK: no rt_* names found under src/frontends/basic/"
  exit 0
fi

echo "Found hardcoded rt_* names under src/frontends/basic/:" >&2
echo "$matches" >&2

if [[ "${VIPER_STRICT_NO_RT_NAMES:-0}" == "1" ]]; then
  echo "FAIL: rt_* usage detected in BASIC frontend (strict mode)." >&2
  exit 1
else
  echo "WARN: rt_* usage detected; set VIPER_STRICT_NO_RT_NAMES=1 to make this fatal." >&2
  exit 0
fi

