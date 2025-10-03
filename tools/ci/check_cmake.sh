#!/usr/bin/env bash
set -euo pipefail
fail=0
grep -R --line-number -E 'link_libraries\s*\(' . && { echo "Do not use link_libraries()"; fail=1; }
grep -R --line-number -E 'target_link_libraries\([^)]*\\.a\b' . && { echo "Do not link .a directly; use targets/IMPORTED"; fail=1; }
exit $fail
