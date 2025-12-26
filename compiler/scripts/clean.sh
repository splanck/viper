#!/usr/bin/env bash
# scripts/clean.sh -- remove CMake build directories safely.
#
# This script removes build directories located at the repository root.
# It prompts for confirmation unless YES=1 is set.
#
# Usage:
#   scripts/clean.sh                 # auto-detect build* dirs at repo root
#   scripts/clean.sh build build-rel # explicit dirs
#   YES=1 scripts/clean.sh           # non-interactive

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mapfile -t DIRS < <(
  if [[ $# -gt 0 ]]; then
    printf '%s\n' "$@"
  else
    find "$ROOT" -maxdepth 1 -type d -name 'build*' -print |
      while IFS= read -r path; do basename "$path"; done
  fi
)
if [[ ${#DIRS[@]} -eq 0 ]]; then echo "[clean] no build* directories found at repo root"; exit 0; fi
echo "[clean] will remove:"; for d in "${DIRS[@]}"; do echo "  - $d"; done
if [[ "${YES:-}" != "1" ]]; then read -r -p "[clean] proceed? [y/N] " ans; [[ "$ans" =~ ^[yY]$ ]] || { echo "[clean] aborted"; exit 1; }; fi
for d in "${DIRS[@]}"; do rm -rf "$ROOT/$d"; echo "[clean] removed $d"; done
echo "[clean] done."
