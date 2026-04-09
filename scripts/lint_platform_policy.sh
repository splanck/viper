#!/usr/bin/env bash
# Lint raw platform-macro usage so new cross-platform policy does not leak into
# random shared code. Default mode is advisory; --strict returns non-zero.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
ALLOWLIST_FILE="$SCRIPT_DIR/platform_policy_allowlist.txt"
STRICT=0
CHANGED_ONLY=0
declare -a PATH_FILTERS=()

usage() {
    cat <<'EOF'
usage: lint_platform_policy.sh [--strict] [--changed-only] [--paths <path>...]

  --strict       Exit non-zero on violations.
  --changed-only Scan files changed from HEAD instead of the whole tracked tree.
  --paths ...    Scan only the provided files/directories.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --strict)
            STRICT=1
            shift
            ;;
        --changed-only)
            CHANGED_ONLY=1
            shift
            ;;
        --paths)
            shift
            while [[ $# -gt 0 && "$1" != --* ]]; do
                PATH_FILTERS+=("$1")
                shift
            done
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ ! -f "$ALLOWLIST_FILE" ]]; then
    echo "error: allowlist file not found: $ALLOWLIST_FILE" >&2
    exit 1
fi

ALLOWLIST=()
while IFS= read -r line; do
    ALLOWLIST+=("$line")
done < <(grep -v '^[[:space:]]*#' "$ALLOWLIST_FILE" | sed '/^[[:space:]]*$/d')

is_allowlisted() {
    local path="$1"
    for pattern in "${ALLOWLIST[@]}"; do
        case "$path" in
            $pattern)
                return 0
                ;;
        esac
    done
    return 1
}

matches_filter() {
    local path="$1"
    if [[ ${#PATH_FILTERS[@]} -eq 0 ]]; then
        return 0
    fi
    for filter in "${PATH_FILTERS[@]}"; do
        case "$path" in
            "$filter"|"$filter"/*)
                return 0
                ;;
        esac
    done
    return 1
}

collect_candidates() {
    if [[ ${#PATH_FILTERS[@]} -gt 0 ]]; then
        for filter in "${PATH_FILTERS[@]}"; do
            if [[ -d "$ROOT_DIR/$filter" ]]; then
                (cd "$ROOT_DIR" && find "$filter" -type f)
            elif [[ -f "$ROOT_DIR/$filter" ]]; then
                printf '%s\n' "$filter"
            fi
        done
        return
    fi

    if [[ $CHANGED_ONLY -eq 1 ]]; then
        (cd "$ROOT_DIR" && git diff --name-only --diff-filter=ACMRTUXB HEAD)
    else
        (cd "$ROOT_DIR" && git ls-files)
    fi
}

is_source_like() {
    local path="$1"
    case "$path" in
        *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.m|*.mm|*.cmake|*/CMakeLists.txt)
            return 0
            ;;
    esac
    return 1
}

declare -a VIOLATIONS=()
declare -a TOUCHPOINT_VIOLATIONS=()
RAW_PATTERN='_WIN32|_WIN64|__APPLE__|__linux__|_MSC_VER'
CANDIDATES=()
while IFS= read -r line; do
    CANDIDATES+=("$line")
done < <(collect_candidates | LC_ALL=C sort -u)

for path in "${CANDIDATES[@]}"; do
    [[ -f "$ROOT_DIR/$path" ]] || continue
    is_source_like "$path" || continue
    matches_filter "$path" || continue

    if ! is_allowlisted "$path"; then
        while IFS= read -r hit; do
            VIOLATIONS+=("$hit")
        done < <(cd "$ROOT_DIR" && rg -n -w "$RAW_PATTERN" --color never "$path" || true)
    fi

    case "$path" in
        src/common/RunProcess.cpp|src/codegen/common/LinkerSupport.cpp|src/codegen/common/RuntimeComponents.hpp|src/codegen/common/linker/NativeLinker.cpp|src/codegen/x86_64/CodegenPipeline.cpp|src/codegen/aarch64/CodegenPipeline.cpp)
            if ! head -n 30 "$ROOT_DIR/$path" | grep -q "Cross-platform touchpoints:"; then
                TOUCHPOINT_VIOLATIONS+=("$path")
            fi
            ;;
    esac
done

if [[ ${#VIOLATIONS[@]} -eq 0 && ${#TOUCHPOINT_VIOLATIONS[@]} -eq 0 ]]; then
    echo "Platform policy lint: clean"
    exit 0
fi

if [[ ${#VIOLATIONS[@]} -gt 0 ]]; then
    echo "Platform policy lint: raw host/compiler macros outside allowlist:" >&2
    printf '  %s\n' "${VIOLATIONS[@]}" >&2
fi

if [[ ${#TOUCHPOINT_VIOLATIONS[@]} -gt 0 ]]; then
    echo "Platform policy lint: missing touchpoint header note:" >&2
    printf '  %s\n' "${TOUCHPOINT_VIOLATIONS[@]}" >&2
fi

if [[ $STRICT -eq 1 ]]; then
    exit 1
fi

echo "Platform policy lint ran in advisory mode; rerun with --strict to fail on these issues." >&2
exit 0
