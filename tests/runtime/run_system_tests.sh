#!/bin/bash
# Run System tests (DateTime, Environment, Diagnostics)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ZIA="${PROJECT_ROOT}/build/zia"

echo "=== System Tests ==="
echo ""

"$ZIA" "$SCRIPT_DIR/test_datetime.zia" && \
"$ZIA" "$SCRIPT_DIR/test_environment.zia" && \
"$ZIA" "$SCRIPT_DIR/test_diagnostics.zia"
