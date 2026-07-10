#!/bin/bash
# Run Math & Numeric tests
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ZIA="${PROJECT_ROOT}/build/zia"

echo "=== Math & Numeric Tests ==="
echo ""

"$ZIA" "$SCRIPT_DIR/test_math.zia" && \
"$ZIA" "$SCRIPT_DIR/test_bits.zia" && \
"$ZIA" "$SCRIPT_DIR/test_random.zia"
