#!/bin/bash
# Run Graphics tests (Color, Pixels, Canvas)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ZIA="${PROJECT_ROOT}/build/zia"

echo "=== Graphics Tests ==="
echo ""

"$ZIA" "$SCRIPT_DIR/test_graphics.zia" && \
"$ZIA" "$SCRIPT_DIR/test_gui.zia"
