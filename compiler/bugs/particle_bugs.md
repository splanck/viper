# Particle Demo Bugs

Bugs discovered while building the particle system demo.

---

## 1. Documentation inconsistency: Seq.Count vs Seq.Len

**Found:** While using Viper.Collections.Seq
**Issue:** Documentation in viperlib shows `Count` property, but the actual runtime uses `Len`
**Resolution:** Use `Len` instead of `Count`
**Status:** Not a bug - documentation issue only

### Root Cause Analysis

This is not actually a bug. The runtime uses different property names for different collection types:
- `Viper.Collections.List` uses `Count` (mutable list with random access)
- `Viper.Collections.Seq` uses `Len` (immutable sequence)
- `Viper.Collections.Set` uses `Len`
- `Viper.Collections.Map` uses `Len`

The naming follows a convention where `List` (the mutable container with index-based access) uses `Count` like .NET collections, while other containers use the shorter `Len`. Documentation should clarify this distinction.

---

## 2. Viper.Random.Next() crashes in native mode on second call

**Found:** When using Viper.Random.Next() in an Emitter class
**Issue:** First call to Viper.Random.Next() works, but subsequent calls in native compiled code cause crash
**Steps to reproduce:**
```basic
' This crashes on second loop iteration in native mode:
WHILE NOT canvas.ShouldClose
    DIM x AS INTEGER = Viper.Random.Next() MOD 100
WEND
```
**VM Behavior:** Works correctly
**Native Behavior:** Crashes on second call
**Workaround:** Use `Viper.Random.NextInt(max)` instead of `Viper.Random.Next() MOD max`
**Status:** FIXED - corrected `cast.fp_to_si.rte.chk` implementation in native codegen

### Root Cause Analysis

**The crash is not in `Random.Next()` itself**, but in the implicit float-to-integer conversion that happens when assigning the result to an `INTEGER` variable.

The BASIC statement `DIM x AS INTEGER = Viper.Random.Next()` generates this IL:
```
%t7 = call @Viper.Random.Next()           ; returns f64 in [0, 1)
%t8 = cast.fp_to_si.rte.chk %t7           ; convert to integer
```

**The IL spec states** (docs/il-guide.md:590):
- `.rte` = round-to-even (IEEE 754 default rounding)
- `.chk` = trap on overflow only

**VM implementation** (src/vm/fp_ops.cpp:497):
- Uses `std::nearbyint()` for round-to-even
- Only checks for overflow (NaN, infinity, out of range)
- Does NOT check for exactness

**Native implementation** (src/codegen/aarch64/fastpaths/FastPaths_Cast.cpp:166-177):
```asm
fcvtzs x0, d0      ; truncate toward zero (WRONG - should round)
scvtf  d1, x0      ; convert back to float
fcmp   d0, d1      ; compare with original
b.ne   trap        ; trap if not exact (WRONG - not in spec)
```

**Two bugs in native codegen:**
1. **Wrong rounding mode**: Uses `fcvtzs` (truncate toward zero) instead of round-to-even
2. **Incorrect exactness check**: Traps if the conversion isn't exact, but spec only requires overflow check

Since `Random.Next()` returns values like 0.534, 0.87, etc., these will never convert exactly to integers (0.534 truncates to 0, but 0.534 != 0.0), so native always traps.

**Fix locations:**
- `src/codegen/aarch64/fastpaths/FastPaths_Cast.cpp` - Remove exactness check, use proper rounding
- `src/codegen/aarch64/OpcodeDispatch.cpp` - Same changes for non-fastpath codegen

### Fix Applied

The fix adds proper round-to-even semantics using ARM64's `frintn` instruction:

**Before (incorrect):**
```asm
fcvtzs x0, d0      ; truncate toward zero
scvtf  d1, x0      ; convert back
fcmp   d0, d1      ; check exactness
b.ne   trap        ; trap if not exact
```

**After (correct per IL spec):**
```asm
frintn d0, d0      ; round to nearest even
fcvtzs x0, d0      ; convert (now exact since value is integral)
```

Files modified:
- `src/codegen/aarch64/MachineIR.hpp` - Added `FRintN` MOpcode
- `src/codegen/aarch64/MachineIR.cpp` - Added opcode name
- `src/codegen/aarch64/AsmEmitter.hpp` - Added `emitFRintN` declaration
- `src/codegen/aarch64/AsmEmitter.cpp` - Added `emitFRintN` implementation
- `src/codegen/aarch64/generated/OpcodeDispatch.inc` - Added `FRintN` dispatch case
- `src/codegen/aarch64/fastpaths/FastPaths_Cast.cpp` - Fixed `CastFpToSiRteChk` fastpath
- `src/codegen/aarch64/OpcodeDispatch.cpp` - Fixed `CastFpToSiRteChk` general path

---

## 3. Integer division doesn't auto-promote to DOUBLE

**Found:** In GetColor() function calculating fade ratio
**Issue:** `Life / MaxLife` where both are INTEGER returns 0 or 1, not a fractional value
**Expected:** Automatic promotion to floating point when assigned to DOUBLE variable
**Actual:** Integer division truncates, fade is always 0 (particles invisible) or 1
**Resolution:** Use explicit `CDBL(Life) / CDBL(MaxLife)` for floating point division
**Status:** By design - not a bug

### Root Cause Analysis

**This is by-design behavior**, consistent with traditional BASIC semantics.

The IL generated for `result = a / b` (where a, b are INTEGER):
```
%t5 = sdiv.chk0 %t3, %t4    ; integer division: 3 / 4 = 0
%t6 = sitofp %t5            ; convert result to float: 0 -> 0.0
store f64, %t0, %t6         ; store in DOUBLE variable
```

**Division happens first** in the type of the operands, then the result is converted to the destination type. This matches:
- QBasic
- Visual Basic 6
- VB.NET
- Most BASIC dialects

This is actually intentional - if you want float division, you must explicitly convert operands:
```basic
result = CDBL(a) / CDBL(b)   ' Correct: 0.75
```

The `/` operator preserves operand types. If automatic promotion were added, it would break existing BASIC conventions and make behavior inconsistent.

**Recommendation:** Document this behavior clearly in the BASIC reference guide, similar to how VB documentation handles it.

---
