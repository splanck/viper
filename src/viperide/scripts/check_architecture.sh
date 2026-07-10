#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"

failures=0
default_limit=600
probe_limit=1700

limit_for() {
    case "$1" in
        src/main.zia) echo 800 ;;
        src/ui/app_shell.zia) echo 2050 ;;
        src/commands/edit_commands.zia) echo 1325 ;;
        src/editor/completion.zia) echo 1300 ;;
        src/commands/search_commands.zia) echo 925 ;;
        src/core/project_manager.zia) echo 900 ;;
        src/commands/file_commands.zia) echo 900 ;;
        src/core/document_manager.zia) echo 650 ;;
        src/ui/ide_overlays.zia) echo 750 ;;
        src/build/debug_session.zia) echo 500 ;;
        src/build/build_system.zia) echo 450 ;;
        src/probes/*) echo "$probe_limit" ;;
        *) echo "$default_limit" ;;
    esac
}

echo "ViperIDE architecture guard"
echo
echo "Large files:"

while IFS= read -r file; do
    lines="$(wc -l < "$file" | tr -d ' ')"
    limit="$(limit_for "$file")"
    if [ "$lines" -gt 500 ]; then
        printf '  %5d / %5d  %s\n' "$lines" "$limit" "$file"
    fi
    if [ "$lines" -gt "$limit" ]; then
        echo "ERROR: $file has $lines lines; budget is $limit." >&2
        failures=$((failures + 1))
    fi
done < <(find src -name '*.zia' -type f | sort)

echo
echo "Teaching headers:"

while IFS= read -r file; do
    if ! sed -n '1,90p' "$file" | grep -q 'MODULE:'; then
        echo "ERROR: $file lacks a MODULE teaching header near the top." >&2
        failures=$((failures + 1))
    fi
done < <(find src -name '*.zia' -type f | sort)

if [ "$failures" -ne 0 ]; then
    echo
    echo "Architecture guard failed with $failures issue(s)." >&2
    exit 1
fi

echo "Architecture guard passed."
