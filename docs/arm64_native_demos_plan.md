# ARM64 Native Demo Compilation Plan

This document outlines the work required to compile and run chess, vtris, and frogger demos as native ARM64 binaries.

## Current Status

**Assembly Generation**: ✅ WORKING

- All three demos (chess, vtris, frogger) successfully generate valid ARM64 assembly
- All ARM64 codegen bugs (ARM-004 through ARM-009) have been fixed

**Linking**: ✅ WORKING

- All symbol mappings added to `mapRuntimeSymbol()` in `AsmEmitter.cpp`
- All three demos (chess, vtris, frogger) compile, assemble, and link successfully

**Testing**: ✅ WORKING

- ctest tests added for ARM64 native linking (`arm64_native_chess`, `arm64_native_vtris`, `arm64_native_frogger`)
- All 747 tests pass

## Implementation Summary (Completed 2025-12-02)

### Phase 1: Symbol Mappings Added

Symbol mappings are centralized in `src/il/runtime/RuntimeNameMap.hpp` via the `mapCanonicalRuntimeName()` function. The AsmEmitter calls this function to resolve `Viper.*` names to their C runtime equivalents (e.g., `rt_term_cls`, `rt_inkey_str`).

Key mappings include:
- Terminal operations: `Viper.Terminal.Clear` → `rt_term_cls`
- String operations: `Viper.String.FromI32` → `rt_str_i32_alloc`
- Parsing: `Viper.Parse.Int64` → `rt_parse_int64`
- Object methods: `Viper.Object.Equals` → `rt_obj_equals`

See `src/il/runtime/RuntimeNameMap.hpp` for the complete mapping table.

### Phase 2: Demo Compilation Verified

All three demos compile and link:

```bash
# Chess (357KB binary)
./build/src/tools/viper/viper front basic -emit-il demos/basic/chess/chess.bas > /tmp/chess.il
./build/src/tools/viper/viper codegen arm64 /tmp/chess.il -S /tmp/chess.s
as /tmp/chess.s -o /tmp/chess.o
clang++ /tmp/chess.o build/src/runtime/libviper_runtime.a -o /tmp/chess_native

# Vtris (298KB binary)
./build/src/tools/viper/viper front basic -emit-il demos/basic/vtris/vtris.bas > /tmp/vtris.il
./build/src/tools/viper/viper codegen arm64 /tmp/vtris.il -S /tmp/vtris.s
as /tmp/vtris.s -o /tmp/vtris.o
clang++ /tmp/vtris.o build/src/runtime/libviper_runtime.a -o /tmp/vtris_native

# Frogger (271KB binary)
./build/src/tools/viper/viper front basic -emit-il demos/basic/frogger/frogger.bas > /tmp/frogger.il
./build/src/tools/viper/viper codegen arm64 /tmp/frogger.il -S /tmp/frogger.s
as /tmp/frogger.s -o /tmp/frogger.o
clang++ /tmp/frogger.o build/src/runtime/libviper_runtime.a -o /tmp/frogger_native
```

### Phase 3: Tests Added

New ctest tests in `src/tests/e2e/CMakeLists.txt`:

- `arm64_native_chess` - Full pipeline test for chess demo
- `arm64_native_vtris` - Full pipeline test for vtris demo
- `arm64_native_frogger` - Full pipeline test for frogger demo

Test script: `src/tests/e2e/test_arm64_native_link.cmake`

## Files Modified

| File                                         | Changes                                                              |
|----------------------------------------------|----------------------------------------------------------------------|
| `src/il/runtime/RuntimeNameMap.hpp`          | Centralized symbol mappings for `Viper.*` to C runtime names         |
| `src/codegen/aarch64/AsmEmitter.cpp`         | Uses `mapCanonicalRuntimeName()` for symbol resolution               |
| `src/tests/e2e/CMakeLists.txt`               | Added 3 ARM64 native linking tests                                   |
| `src/tests/e2e/test_arm64_native_link.cmake` | New test script for full compile/assemble/link pipeline              |

## Success Criteria (All Met)

1. ✅ All three demos compile without linker errors
2. ✅ All three demos produce valid native ARM64 binaries
3. ✅ No new test failures in the test suite (747/747 pass)
4. ✅ ctest tests added for regression testing

## Related Bugs Fixed

- **BUG-ARM-004**: Stack offset exceeds ARM64 immediate range - Fixed
- **BUG-ARM-005**: Duplicate trap labels - Fixed
- **BUG-ARM-009**: Frontend wrong parameter name in IL - Fixed (root cause of ARM-006, ARM-008)
