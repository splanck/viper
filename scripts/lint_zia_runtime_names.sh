#!/usr/bin/env bash
# Guard Zia lowerer code against handwritten canonical runtime-name literals.
# Runtime names must come from generated il/runtime/RuntimeNames.hpp constants
# so renames in runtime.def fail at compile time instead of surfacing as a
# missing runtime export later.
set -euo pipefail

ROOT_DIR="${1:-}"
if [[ -z "$ROOT_DIR" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    ROOT_DIR="$(dirname "$SCRIPT_DIR")"
fi

declare -a SCAN_PATHS=(
    "src/frontends/zia/Lowerer_*.cpp"
    "src/frontends/zia/RuntimeNames.hpp"
)

cd "$ROOT_DIR"

matches=""
if command -v rg >/dev/null 2>&1; then
    matches="$(rg -n '"Viper\.' ${SCAN_PATHS[@]} || true)"
else
    matches="$(grep -R -n '"Viper\.' src/frontends/zia/Lowerer_*.cpp \
        src/frontends/zia/RuntimeNames.hpp || true)"
fi

if [[ -n "$matches" ]]; then
    cat >&2 <<'EOF'
error: Zia lowerer runtime names must use generated RuntimeNames.hpp constants.
Replace quoted Viper.* strings with il::runtime::names constants or Zia aliases.

EOF
    printf '%s\n' "$matches" >&2
    exit 1
fi

echo "PASS zia runtime-name literal lint"
