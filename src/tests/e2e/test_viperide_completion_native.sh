#!/bin/bash
set -euo pipefail

VIPER_BIN="$1"
ROOT_DIR="$2"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/viperide_completion_native.XXXXXX")"
trap 'rm -rf "$TMP_DIR"' EXIT

IL_FILE="$TMP_DIR/viperide_completion_probe.il"
BIN_FILE="$TMP_DIR/viperide_completion_probe"
ARCH="${VIPER_NATIVE_TEST_ARCH:-}"

if [[ -z "$ARCH" ]]; then
    case "$(uname -m)" in
        arm64|aarch64)
            ARCH="arm64"
            ;;
        x86_64|amd64)
            ARCH="x64"
            ;;
        *)
            echo "Unsupported native test architecture: $(uname -m)" >&2
            exit 1
            ;;
    esac
fi

"$VIPER_BIN" build "$ROOT_DIR/src/tests/e2e/viperide_completion_native_probe.zia" -o "$IL_FILE"
"$VIPER_BIN" codegen "$ARCH" "$IL_FILE" --native-asm --native-link -O1 -o "$BIN_FILE"

OUTPUT="$("$BIN_FILE")"
printf '%s\n' "$OUTPUT"

grep -q '^RESULT: ok$' <<<"$OUTPUT"
echo "PASS"
