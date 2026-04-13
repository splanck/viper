#!/bin/bash
set -euo pipefail

ROOT_DIR="$1"

OUTPUT="$("$ROOT_DIR/scripts/lint_platform_policy.sh" --paths scripts/definitely_missing_path_for_empty_lint_candidates)"
printf '%s\n' "$OUTPUT"

grep -q '^Platform policy lint: clean$' <<<"$OUTPUT"

echo "PASS"
