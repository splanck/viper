# Windows x86_64 Backend Bugs

This file tracks bugs and missing features discovered while getting Viper's x86_64 backend working on Windows.

## Status Summary

| Category | Status |
|----------|--------|
| VM Execution | Working (99.5% tests pass) |
| x86_64 Codegen | Working |
| Native Linking | Requires clang + MSVC runtime libs |

## Fixed Bugs

### BUG-001: x86_64 Operand Order in movq (FIXED)

**Status:** Fixed
**Severity:** Critical
**Component:** src/codegen/x86_64/AsmEmitter.cpp

**Description:**
The x86_64 backend's `getFmt()` function used binary search (`lower_bound`) on the `kOpFmt` array, but the array was not sorted by opcode value. This caused lookup failures for some opcodes, falling back to emitting operands in Intel order instead of AT&T order.

**Fix:**
Changed `getFmt()` to use linear search instead of binary search since the OpFmtTable order doesn't match the MOpcode enum order.

---

### BUG-002: xorl with 64-bit registers (FIXED)

**Status:** Fixed
**Severity:** Critical
**Component:** src/codegen/x86_64/generated/OpFmtTable.inc

**Description:**
XORrr32 instruction was emitting `xorl %rdi, %rdi` (64-bit registers) instead of `xorl %edi, %edi` (32-bit registers). The `xorl` instruction requires 32-bit register names.

**Fix:**
Added `kFmtReg32` flag to XORrr32 entry in OpFmtTable.inc.

---

### BUG-003: MOVSDmr pattern order (FIXED)

**Status:** Fixed
**Severity:** Critical
**Component:** src/codegen/x86_64/generated/EncodingTable.inc

**Description:**
The MOVSDmr (memory to register load for scalar double) instruction had incorrect operand pattern `(Mem, Reg)` in the EncodingTable, but the lowering code creates operands in `(Reg, Mem)` order.

**Fix:**
Changed MOVSDmr pattern from `makePattern(OperandKind::Mem, OperandKind::Reg)` to `makePattern(OperandKind::Reg, OperandKind::Mem)`.

---

### BUG-004: .note.GNU-stack on Windows (FIXED)

**Status:** Fixed
**Severity:** Minor
**Component:** src/tests/e2e/test_codegen.cmake

**Description:**
The `codegen_assemble_link` test used `.section .note.GNU-stack,"",@progbits` directive which is Linux-specific and not recognized by clang on Windows.

**Fix:**
Made the directive conditional on platform (only include on non-Windows).

---

## Test Results

### Tests: 859/863 passing (99.5%)

Remaining failures (pre-existing, not codegen-related):
- test_tools_module_loader - Path handling issue
- vm_break_src_exact - Debugger breakpoint test
- vm_break_src_coalesce - Debugger breakpoint test
- zia_runtime_test_gui - GUI test (Windows-specific exit code)

### BASIC Demos Native Build Status

| Demo | VM Status | Native Status | Notes |
|------|-----------|---------------|-------|
| frogger | Working | Working | 132KB exe |
| vtris | Working | Working | 107KB exe |
| chess | Working | Working | Linked successfully |
| centipede | Untested | Untested | |
| pacman | Untested | Untested | |

## Build Notes

### Linking Native Executables on Windows

To link a native executable on Windows:

```bash
# 1. Compile BASIC to IL
./build/src/tools/ilc/Debug/ilc.exe front basic -emit-il demo.bas > demo.il

# 2. Generate x86_64 assembly
./build/src/tools/ilc/Debug/ilc.exe codegen x64 demo.il -S demo.s

# 3. Assemble
clang -c demo.s -o demo.o

# 4. Link with runtime (need MSVC debug libs for Debug builds)
clang++ demo.o build/src/runtime/Debug/viper_runtime.lib -lmsvcrtd -lucrtd -o demo.exe
```

---

*Last updated: 2026-01-17*
