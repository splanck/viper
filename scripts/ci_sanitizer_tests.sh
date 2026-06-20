#!/usr/bin/env bash
# Script: ci_sanitizer_tests.sh
# Purpose: Compatibility wrapper for the canonical sanitizer lane.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cat >&2 <<'EOF'
[sanitizer] scripts/ci_sanitizer_tests.sh is a compatibility wrapper.
[sanitizer] Delegating to scripts/ci_full_sanitizer.sh for the broad ASan/UBSan lane.
EOF

exec "${SCRIPT_DIR}/ci_full_sanitizer.sh" "$@"
