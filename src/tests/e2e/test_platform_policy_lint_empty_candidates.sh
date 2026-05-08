#!/bin/bash
set -euo pipefail

to_shell_path() {
    local path="$1"
    if [[ "$path" =~ ^[A-Za-z]:[\\/] ]] && command -v wslpath >/dev/null 2>&1; then
        wslpath "$path"
        return
    fi
    if [[ "$path" =~ ^[A-Za-z]:[\\/] ]] && command -v cygpath >/dev/null 2>&1; then
        cygpath -u "$path"
        return
    fi
    printf '%s\n' "$path"
}

ROOT_DIR="$(to_shell_path "$1")"

OUTPUT="$("$ROOT_DIR/scripts/lint_platform_policy.sh" --paths scripts/definitely_missing_path_for_empty_lint_candidates)"
printf '%s\n' "$OUTPUT"

grep -q '^Platform policy lint: clean$' <<<"$OUTPUT"

echo "PASS"
