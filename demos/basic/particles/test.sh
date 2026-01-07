#!/bin/bash
# Test particle demo in both VM and native modes
set -e

cd /Users/stephen/git/viper

echo "=== Building and testing particle demo ==="
echo ""

echo "--- VM Mode ---"
./build/src/tools/vbasic/vbasic demos/basic/particles/main.bas &
PID=$!
sleep 3
kill $PID 2>/dev/null || true
echo "VM test done"

echo ""
echo "--- Native Mode ---"
./build/src/tools/vbasic/vbasic demos/basic/particles/main.bas -o /tmp/particles.il
./build/src/tools/ilc/ilc codegen arm64 /tmp/particles.il -S /tmp/particles.s
as /tmp/particles.s -o /tmp/particles.o
clang++ /tmp/particles.o \
  build/src/runtime/libviper_runtime.a \
  build/lib/libvipergfx.a \
  -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore \
  -o /tmp/particles

/tmp/particles &
PID=$!
sleep 3
kill $PID 2>/dev/null || true
echo "Native test done"

echo ""
echo "=== All tests completed ==="
