#!/usr/bin/env bash
# Script: clean.sh
# Purpose: Remove generated build and report directories safely.
#
# This script removes generated directories located at the repository root.
# It prompts for confirmation unless YES=1 is set.
#
# Usage:
#   scripts/clean.sh                 # auto-detect generated dirs at repo root
#   scripts/clean.sh build build-rel # explicit dirs
#   YES=1 scripts/clean.sh           # non-interactive

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIRS=()
if [[ $# -gt 0 ]]; then
  for d in "$@"; do DIRS+=("$d"); done
else
  while IFS= read -r path; do
    DIRS+=("$(basename "$path")")
  done < <(find "$ROOT" -maxdepth 1 -type d \( -name 'build*' -o -name 'cmake-build*' -o -name 'coverage' \) -print)
fi
if [[ ${#DIRS[@]} -eq 0 ]]; then echo "[clean] no generated build/report directories found at repo root"; exit 0; fi
echo "[clean] will remove:"; for d in "${DIRS[@]}"; do echo "  - $d"; done
if [[ "${YES:-}" != "1" ]]; then read -r -p "[clean] proceed? [y/N] " ans; [[ "$ans" =~ ^[yY]$ ]] || { echo "[clean] aborted"; exit 1; }; fi
for d in "${DIRS[@]}"; do
  case "$d" in
    ""|"."|".."|"/"|"$ROOT") echo "[clean] refusing unsafe path: $d" >&2; exit 2 ;;
  esac
  rm -rf "$ROOT/$d"
  echo "[clean] removed $d"
done
echo "[clean] done."
