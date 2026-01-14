#!/bin/bash
# Test compilation of a single demo through the full pipeline
# Usage: ./scripts/test_demo_compile.sh <demo_type> <source_file>
#   demo_type: "basic" or "viperlang"
#   source_file: path to .bas or .viper file
# Exit codes: 0 = success, 1 = failure

set -e

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <demo_type> <source_file>"
    exit 1
fi

DEMO_TYPE="$1"
SOURCE_FILE="$2"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/build"

ILC="$BUILD_DIR/src/tools/ilc/ilc"
VIPER="$BUILD_DIR/src/tools/viper/viper"
RUNTIME_LIB="$BUILD_DIR/src/runtime/libviper_runtime.a"
GFX_LIB="$BUILD_DIR/lib/libvipergfx.a"
GUI_LIB="$BUILD_DIR/src/lib/gui/libvipergui.a"

# macOS frameworks needed for graphics demos
MACOS_GFX_FRAMEWORKS="-framework Cocoa -framework IOKit -framework CoreFoundation"

TMP_DIR=$(mktemp -d)
trap "rm -rf $TMP_DIR" EXIT

NAME=$(basename "$SOURCE_FILE" | sed 's/\.[^.]*$//')
IL_FILE="$TMP_DIR/${NAME}.il"
ASM_FILE="$TMP_DIR/${NAME}.s"
OBJ_FILE="$TMP_DIR/${NAME}.o"
EXE_FILE="$TMP_DIR/${NAME}"

# Step 1: Compile source to IL
echo "Step 1: Compiling $DEMO_TYPE to IL..."
if [[ "$DEMO_TYPE" == "basic" ]]; then
    "$ILC" front basic -emit-il "$SOURCE_FILE" > "$IL_FILE"
elif [[ "$DEMO_TYPE" == "viperlang" ]]; then
    "$VIPER" "$SOURCE_FILE" --emit-il > "$IL_FILE"
else
    echo "Unknown demo type: $DEMO_TYPE"
    exit 1
fi

# Step 2: Generate ARM64 assembly
echo "Step 2: Generating ARM64 assembly..."
"$ILC" codegen arm64 "$IL_FILE" -S "$ASM_FILE"

# Step 3: Assemble
echo "Step 3: Assembling..."
as "$ASM_FILE" -o "$OBJ_FILE"

# Step 4: Link
echo "Step 4: Linking..."
if [[ "$DEMO_TYPE" == "basic" ]]; then
    clang++ "$OBJ_FILE" "$RUNTIME_LIB" -o "$EXE_FILE"
elif [[ "$DEMO_TYPE" == "viperlang" ]]; then
    clang++ "$OBJ_FILE" "$RUNTIME_LIB" "$GFX_LIB" "$GUI_LIB" $MACOS_GFX_FRAMEWORKS -o "$EXE_FILE"
fi

echo "Success: $NAME compiled and linked successfully"
exit 0
