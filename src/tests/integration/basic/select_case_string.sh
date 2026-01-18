#!/usr/bin/env bash
set -euo pipefail

readonly BUILD_DIR="${1?usage: $0 <build-dir>}"

TMP="$(mktemp)"
cat > "$TMP" <<'BAS'
LET S$ = "dog"
GOSUB Speak
LET S$ = "emu"
GOSUB Speak
END

Speak:
  SELECT CASE S$
    CASE "cat": PRINT "meow"
    CASE "dog": PRINT "woof"
    CASE ELSE:  PRINT "???"
  END SELECT
RETURN
BAS

OUT="$("${BUILD_DIR}/src/tools/viper/viper" front basic -run "$TMP")"
if [[ "$OUT" != $'woof\n???' ]]; then
  echo "Unexpected output:"
  printf '%s\n' "$OUT"
  exit 1
fi
