# ARM64 Codegen Bugs

Discovered during ARM64 codegen test coverage expansion.

---

## ARM64-BUG-001: call.indirect generates no code

**Severity:** High
**Status:** FIXED

**Symptom:** The `call.indirect` opcode is parsed successfully but generates no assembly code. The function containing `call.indirect` simply returns without making any call.

**Test case:**
```
il 0.1
func @target() -> i64 {
entry:
  ret 42
}
func @caller() -> i64 {
entry:
  %r = call.indirect @target()
  ret %r
}
```

**Fix:** Added `Opcode::CallIndirect` handling in `/src/codegen/aarch64/OpcodeDispatch.cpp` that generates `adrp`/`add` to load the function address, then `blr` for the indirect call. Also fixed `Peephole.cpp` to correctly recognize `blr` as using its register operand (was incorrectly classifying it as a DEF causing dead code elimination to remove the mov instruction).

**Files modified:**
- `src/codegen/aarch64/OpcodeDispatch.cpp` - Added CallIndirect handler
- `src/codegen/aarch64/MachineIR.hpp` - Added Blr opcode
- `src/codegen/aarch64/MachineIR.cpp` - Added Blr name mapping
- `src/codegen/aarch64/AsmEmitter.cpp` - Added Blr emission
- `src/codegen/aarch64/Peephole.cpp` - Added Blr handling in usesReg, hasSideEffects, classifyOperand

---

## ARM64-BUG-002: call.indirect with arguments fails to parse

**Severity:** Medium
**Status:** FIXED

**Symptom:** `call.indirect @target(%arg)` failed with "malformed global name" error.

**Root cause:** The `CallIndirect` opcode used `OperandParseKind::Value` which only parses a single value, not call syntax with arguments.

**Fix:** Changed `CallIndirect`'s parse spec in `Opcode.def` to use `OperandParseKind::Call` (same as regular `Call` opcode). Now uses `call.indirect @target()` syntax with parentheses.

**Files modified:**
- `src/il/core/Opcode.def` - Changed CallIndirect parse spec from Value to Call

---

## ARM64-BUG-003: Shift amount from computed value uses wrong operand

**Severity:** Medium
**Status:** FIXED

**Symptom:** When a shift amount comes from a computation (e.g., `%amt = add %base, 1; %r = shl %val, %amt`), the generated code used `lsl x, x, #0` instead of the computed register value.

**Root cause:** Two issues:
1. `OpcodeMappings.hpp` used `LslRI` as the `mirOp` (register-register-register form) when it should have used a register-based shift opcode
2. `InstrLowering.cpp` had an `!isShift` guard that prevented using the RRR form for shifts

**Fix:**
1. Added new opcodes `LslvRRR`, `LsrvRRR`, `AsrvRRR` for register-based variable shifts
2. Updated `OpcodeMappings.hpp` to use these as the `mirOp` for shift operations
3. Removed the `!isShift` guard in `InstrLowering.cpp`
4. Added emit functions for `lslv`, `lsrv`, `asrv` instructions
5. Updated `Peephole.cpp` with the new opcodes for liveness analysis

**Files modified:**
- `src/codegen/aarch64/MachineIR.hpp` - Added LslvRRR, LsrvRRR, AsrvRRR opcodes
- `src/codegen/aarch64/MachineIR.cpp` - Added opcode name mappings
- `src/codegen/aarch64/OpcodeMappings.hpp` - Updated shift mappings to use register form
- `src/codegen/aarch64/InstrLowering.cpp` - Removed !isShift guard
- `src/codegen/aarch64/AsmEmitter.cpp` - Added emit functions for lslv/lsrv/asrv
- `src/codegen/aarch64/AsmEmitter.hpp` - Added emit function declarations
- `src/codegen/aarch64/Peephole.cpp` - Added new opcodes to all switch statements

---

## Test Status Summary

**Final Results: 61/65 tests passing (94%)**

**Tests Fixed During This Session:**
- `test_codegen_arm64_indirect_call` - All 4 subtests now pass (BUG-001 + BUG-002)
- `test_codegen_arm64_shift_reg` - All 6 subtests now pass (BUG-003)
- `test_codegen_arm64_idxchk` - Added IdxChk opcode handling
- `test_codegen_arm64_division` - Added SRem/URem opcodes, fixed zero-check test assertions
- `test_codegen_arm64_cross_block_phi_spill` - Fixed IL syntax (6 tests)
- `test_codegen_arm64_spill_fpr` - Fixed LoopAccumulator test IL syntax
- `test_codegen_arm64_callee_saved` - Fixed LoopWithCall test IL syntax

**Remaining 4 Failures (IL Spec Limitations - NOT Codegen Bugs):**

These tests use IL opcodes/syntax that aren't implemented in the IL spec. They are test design issues, not ARM64 codegen bugs:

1. `test_codegen_arm64_string_store_refcount` - Uses `store.str` opcode (not in IL spec)
2. `test_codegen_arm64_fp_cmp_all` - Uses `fcmp_ord` opcode (not in IL spec, only fcmp_eq/ne/lt/le/gt/ge exist)
3. `test_codegen_arm64_gaddr_constnull` - Uses `global @name : i64` syntax (IL only supports `global const str`)
4. `test_codegen_arm64_rodata_literals` - Uses `const.f64` opcode (not in IL spec)

These would need IL spec changes to support, not ARM64 codegen fixes.

---
