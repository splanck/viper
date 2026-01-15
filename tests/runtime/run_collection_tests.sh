#!/bin/bash
# Run Collection tests (List, Map, Seq, Stack, Queue, etc.)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ZIA="${PROJECT_ROOT}/build/zia"

echo "=== Collection Tests ==="
echo ""

"$ZIA" "$SCRIPT_DIR/test_list.zia" && \
"$ZIA" "$SCRIPT_DIR/test_map.zia" && \
"$ZIA" "$SCRIPT_DIR/test_collections.zia" && \
"$ZIA" "$SCRIPT_DIR/test_bytes.zia"
