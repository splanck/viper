#!/bin/bash
# Run Cryptography tests
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ZIA="${PROJECT_ROOT}/build/zia"

echo "=== Crypto Tests ==="
echo ""

"$ZIA" "$SCRIPT_DIR/test_crypto.zia"
