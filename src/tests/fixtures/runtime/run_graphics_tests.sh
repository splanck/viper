#!/bin/bash
# Run Graphics tests (Color, Pixels, Canvas)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ -x "${PROJECT_ROOT}/build/src/tools/viper/viper" ]; then
    RUNNER=("${PROJECT_ROOT}/build/src/tools/viper/viper" run)
elif [ -x "${PROJECT_ROOT}/build/src/tools/zia/zia" ]; then
    RUNNER=("${PROJECT_ROOT}/build/src/tools/zia/zia")
elif command -v viper >/dev/null 2>&1; then
    RUNNER=("$(command -v viper)" run)
elif command -v zia >/dev/null 2>&1; then
    RUNNER=("$(command -v zia)")
else
    echo "ERROR: Cannot find a runnable Zia/Viper frontend"
    exit 1
fi

echo "=== Graphics Tests ==="
echo ""

"${RUNNER[@]}" "$SCRIPT_DIR/test_graphics.zia" && \
"${RUNNER[@]}" "$SCRIPT_DIR/test_gui.zia"
