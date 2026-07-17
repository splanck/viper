#!/usr/bin/env bash
# Script: example_smoke.sh
# Purpose: Manifest-driven audit and smoke lane for examples/.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
MANIFEST="${ZANNA_EXAMPLE_SMOKE_MANIFEST:-${ROOT_DIR}/examples/smoke_manifest.tsv}"
ZANNA_BIN="${ZANNA_BIN:-${ROOT_DIR}/build/src/tools/zanna/zanna}"
MODE="audit"

usage() {
    cat <<'EOF'
Usage: scripts/example_smoke.sh [--audit|--fast|--all] [--zanna PATH]

Modes:
  --audit  Verify every examples/*.zia, *.bas, and *.il source is classified.
  --fast   Run the manifest's fast smoke targets.
  --all    Run every check/run target in the manifest.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --audit) MODE="audit"; shift ;;
        --fast) MODE="fast"; shift ;;
        --all) MODE="all"; shift ;;
        --zanna) ZANNA_BIN="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown option: $1" >&2; usage >&2; exit 1 ;;
    esac
done

shell_path() {
    local path="$1"
    case "$path" in
        [A-Za-z]:/*)
            local drive="${path%%:*}"
            local rest="${path#?:/}"
            local drive_lower
            drive_lower="$(printf '%s' "$drive" | tr '[:upper:]' '[:lower:]')"
            local candidate="/mnt/$drive_lower/$rest"
            if [[ -e "$candidate" ]]; then
                printf '%s\n' "$candidate"
                return
            fi
            ;;
    esac
    printf '%s\n' "$path"
}

windows_path() {
    local path="$1"
    case "$path" in
        /mnt/[A-Za-z]/*)
            local drive="${path#/mnt/}"
            drive="${drive%%/*}"
            local rest="${path#/mnt/$drive/}"
            local drive_upper
            drive_upper="$(printf '%s' "$drive" | tr '[:lower:]' '[:upper:]')"
            printf '%s:/%s\n' "$drive_upper" "$rest"
            return
            ;;
    esac
    printf '%s\n' "$path"
}

ZANNA_BIN_USES_WINDOWS_ARGS=0
case "$ZANNA_BIN" in
    [A-Za-z]:/*) ZANNA_BIN_USES_WINDOWS_ARGS=1 ;;
esac
ZANNA_BIN="$(shell_path "$ZANNA_BIN")"
case "$ZANNA_BIN" in
    /mnt/[A-Za-z]/*.exe) ZANNA_BIN_USES_WINDOWS_ARGS=1 ;;
esac

zanna_arg_path() {
    if [[ "$ZANNA_BIN_USES_WINDOWS_ARGS" -eq 1 ]]; then
        windows_path "$1"
    else
        printf '%s\n' "$1"
    fi
}

if [[ ! -f "$MANIFEST" ]]; then
    echo "error: manifest not found: $MANIFEST" >&2
    exit 1
fi

tmpdir="$(mktemp -d)"
trap "rm -rf '$tmpdir'" EXIT
all_sources="$tmpdir/all_sources"
classified_sources="$tmpdir/classified_sources"
targets="$tmpdir/targets"
pattern_failures="$tmpdir/pattern_failures"

find "$ROOT_DIR/examples" -type f \( -name '*.zia' -o -name '*.bas' -o -name '*.il' \) \
    ! -path "$ROOT_DIR/examples/bin/*" \
    | sed "s#^$ROOT_DIR/examples/##" \
    | sort > "$all_sources"
: > "$classified_sources"
: > "$targets"
: > "$pattern_failures"

while IFS=$'\t' read -r pattern lane labels target reason; do
    [[ -z "${pattern:-}" || "${pattern:0:1}" == "#" ]] && continue

    matches=()
    while IFS= read -r rel; do
        case "$rel" in
            $pattern) matches+=("$rel") ;;
        esac
    done < "$all_sources"

    if [[ ${#matches[@]} -eq 0 ]]; then
        echo "$pattern" >> "$pattern_failures"
        continue
    fi

    for rel in "${matches[@]}"; do
        echo "$rel" >> "$classified_sources"
        smoke_target=""
        case "$target" in
            SELF)
                smoke_target="$rel"
                ;;
            PROJECT2)
                IFS=/ read -r seg1 seg2 _rest <<< "$rel"
                smoke_target="$seg1/$seg2"
                ;;
            *)
                smoke_target="$target"
                ;;
        esac

        if [[ "$MODE" == "all" || ( "$MODE" == "fast" && "$lane" == *"-smoke" ) ]]; then
            case "$lane" in
                il-run*) echo -e "il-run\t$smoke_target" >> "$targets" ;;
                check*) echo -e "check\t$smoke_target" >> "$targets" ;;
            esac
        fi
    done
done < "$MANIFEST"

sort -u "$classified_sources" -o "$classified_sources"
sort -u "$targets" -o "$targets"

if [[ -s "$pattern_failures" ]]; then
    echo "example smoke manifest contains patterns with no matches:" >&2
    sed 's/^/  - /' "$pattern_failures" >&2
    exit 1
fi

missing="$tmpdir/missing"
extra="$tmpdir/extra"
comm -23 "$all_sources" "$classified_sources" > "$missing"
comm -13 "$all_sources" "$classified_sources" > "$extra"

if [[ -s "$missing" || -s "$extra" ]]; then
    if [[ -s "$missing" ]]; then
        echo "unclassified example sources:" >&2
        sed 's/^/  - examples\//' "$missing" >&2
    fi
    if [[ -s "$extra" ]]; then
        echo "manifest classified non-source paths:" >&2
        sed 's/^/  - examples\//' "$extra" >&2
    fi
    exit 1
fi

if [[ "$MODE" == "audit" ]]; then
    count="$(wc -l < "$all_sources" | tr -d ' ')"
    echo "example smoke audit: $count sources classified"
    exit 0
fi

if [[ ! -x "$ZANNA_BIN" ]]; then
    echo "error: zanna binary not executable: $ZANNA_BIN" >&2
    exit 1
fi

while IFS=$'\t' read -r action target; do
    [[ -z "${action:-}" ]] && continue
    full_target="$(zanna_arg_path "$ROOT_DIR/examples/$target")"
    case "$action" in
        check)
            echo "[examples] check examples/$target"
            "$ZANNA_BIN" check "$full_target" --diagnostic-format=json >/dev/null
            ;;
        il-run)
            echo "[examples] run examples/$target"
            "$ZANNA_BIN" -run "$full_target" >/dev/null
            ;;
        *)
            echo "error: unknown action: $action" >&2
            exit 1
            ;;
    esac
done < "$targets"

echo "example smoke $MODE: ok"
