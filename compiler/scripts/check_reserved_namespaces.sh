#!/usr/bin/env bash
# Script: check_reserved_namespaces.sh
# Purpose: Ensure user-facing code does not use reserved "Viper" namespace
# Exit code: 0 if clean, 1 if violations found

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "Checking for reserved 'Viper' namespace usage in user-facing code..."

# Directories to check for user-facing code
CHECK_DIRS=(
  "${REPO_ROOT}/tests/golden/basic"
  "${REPO_ROOT}/tests/e2e"
  "${REPO_ROOT}/examples/basic"
)

# Allowed exceptions (built-in library examples showing Track B syntax)
ALLOWED_FILES=(
  "tests/golden/basic/viper_root_example.bas"
  "tests/golden/basic_errors/reserved_root_user_decl.bas"
  "tests/golden/basic_errors/reserved_root_user_using.bas"
)

VIOLATIONS=0

check_file() {
  local file="$1"
  local rel_path="${file#${REPO_ROOT}/}"

  # Check if this file is in the allowed list
  for allowed in "${ALLOWED_FILES[@]}"; do
    if [[ "${rel_path}" == "${allowed}" ]]; then
      return 0
    fi
  done

  # Check for NAMESPACE Viper patterns
  if grep -E '^\s*NAMESPACE\s+Viper' "$file" > /dev/null 2>&1; then
    echo -e "${RED}✗ VIOLATION${NC}: $rel_path contains 'NAMESPACE Viper'"
    grep -n -E '^\s*NAMESPACE\s+Viper' "$file" | head -3
    VIOLATIONS=$((VIOLATIONS + 1))
  fi

  # Check for USING Viper patterns (but not USING Viper.Something)
  # We want to catch "USING Viper" (root) but allow "USING Viper.System.Text" in examples
  if grep -E '^\s*USING\s+Viper\s*($|REM)' "$file" > /dev/null 2>&1; then
    echo -e "${RED}✗ VIOLATION${NC}: $rel_path contains 'USING Viper' (root)"
    grep -n -E '^\s*USING\s+Viper\s*($|REM)' "$file" | head -3
    VIOLATIONS=$((VIOLATIONS + 1))
  fi
}

# Check all .bas files in the target directories
for dir in "${CHECK_DIRS[@]}"; do
  if [[ -d "$dir" ]]; then
    while IFS= read -r -d '' file; do
      check_file "$file"
    done < <(find "$dir" -name "*.bas" -print0)
  fi
done

echo ""
if [[ $VIOLATIONS -eq 0 ]]; then
  echo -e "${GREEN}✓ PASS${NC}: No reserved namespace violations found"
  exit 0
else
  echo -e "${RED}✗ FAIL${NC}: Found $VIOLATIONS violation(s) of reserved 'Viper' namespace"
  echo ""
  echo "The 'Viper' root namespace is reserved for built-in libraries (Track B)."
  echo "User code should not use 'NAMESPACE Viper' or 'USING Viper'."
  echo ""
  echo "If this is intentional (e.g., error test or Track B example),"
  echo "add the file to ALLOWED_FILES in this script."
  exit 1
fi
