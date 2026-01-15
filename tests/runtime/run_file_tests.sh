#!/bin/bash
# Run File I/O tests
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ZIA="${PROJECT_ROOT}/build/zia"

echo "=== File I/O Tests ==="
echo ""

"$ZIA" "$SCRIPT_DIR/test_file.zia"
