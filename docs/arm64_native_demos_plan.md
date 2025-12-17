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

Added ~35 symbol mappings to `mapRuntimeSymbol()` in `src/codegen/aarch64/AsmEmitter.cpp`:

```cpp
// Terminal operations
if (name == "Viper.Terminal.Clear")
    return "rt_term_cls";
if (name == "Viper.Terminal.InKey")
    return "rt_inkey_str";
if (name == "Viper.Terminal.SetColor")
    return "rt_term_color_i32";
if (name == "Viper.Terminal.SetPosition")
    return "rt_term_locate_i32";

// String formatting
if (name == "Viper.Strings.FromI32")
    return "rt_str_i32_alloc";
if (name == "Viper.Strings.FromI16")
    return "rt_str_i16_alloc";
if (name == "Viper.Strings.FromSingle")
    return "rt_str_f_alloc";
if (name == "Viper.Strings.FromDoublePrecise")
    return "rt_str_d_alloc";
if (name == "Viper.Strings.SplitFields")
    return "rt_split_fields";
if (name == "Viper.Strings.Equals")
    return "rt_str_eq";
if (name == "Viper.Strings.FromStr")
    return "rt_str";

// Parsing
if (name == "Viper.Parse.Int64")
    return "rt_parse_int64";
if (name == "Viper.Parse.Double")
    return "rt_parse_double";

// String properties
if (name == "Viper.String.ConcatSelf")
    return "rt_concat";
if (name == "Viper.String.get_IsEmpty")
    return "rt_str_is_empty";

// Object methods
if (name == "Viper.Object.Equals")
    return "rt_obj_equals";
if (name == "Viper.Object.GetHashCode")
    return "rt_obj_get_hash_code";
if (name == "Viper.Object.ReferenceEquals")
    return "rt_obj_reference_equals";
if (name == "Viper.Object.ToString")
    return "rt_obj_to_string";

// StringBuilder properties
if (name == "Viper.Text.StringBuilder.get_Length")
    return "rt_text_sb_get_length";
if (name == "Viper.Text.StringBuilder.get_Capacity")
    return "rt_text_sb_get_capacity";
```

### Phase 2: Demo Compilation Verified

All three demos compile and link:

```bash
# Chess (357KB binary)
./build/src/tools/ilc/ilc front basic -emit-il demos/basic/chess/chess.bas > /tmp/chess.il
./build/src/tools/ilc/ilc codegen arm64 /tmp/chess.il -S /tmp/chess.s
as /tmp/chess.s -o /tmp/chess.o
clang++ /tmp/chess.o build/src/runtime/libviper_runtime.a -o /tmp/chess_native

# Vtris (298KB binary)
./build/src/tools/ilc/ilc front basic -emit-il demos/basic/vtris/vtris.bas > /tmp/vtris.il
./build/src/tools/ilc/ilc codegen arm64 /tmp/vtris.il -S /tmp/vtris.s
as /tmp/vtris.s -o /tmp/vtris.o
clang++ /tmp/vtris.o build/src/runtime/libviper_runtime.a -o /tmp/vtris_native

# Frogger (271KB binary)
./build/src/tools/ilc/ilc front basic -emit-il demos/basic/frogger/frogger.bas > /tmp/frogger.il
./build/src/tools/ilc/ilc codegen arm64 /tmp/frogger.il -S /tmp/frogger.s
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

| File                                         | Changes                                                 |
|----------------------------------------------|---------------------------------------------------------|
| `src/codegen/aarch64/AsmEmitter.cpp`         | Added ~35 symbol mappings to `mapRuntimeSymbol()`       |
| `src/tests/e2e/CMakeLists.txt`               | Added 3 ARM64 native linking tests                      |
| `src/tests/e2e/test_arm64_native_link.cmake` | New test script for full compile/assemble/link pipeline |

## Success Criteria (All Met)

1. ✅ All three demos compile without linker errors
2. ✅ All three demos produce valid native ARM64 binaries
3. ✅ No new test failures in the test suite (747/747 pass)
4. ✅ ctest tests added for regression testing

## Related Bugs Fixed

- **BUG-ARM-004**: Stack offset exceeds ARM64 immediate range - Fixed
- **BUG-ARM-005**: Duplicate trap labels - Fixed
- **BUG-ARM-009**: Frontend wrong parameter name in IL - Fixed (root cause of ARM-006, ARM-008)
