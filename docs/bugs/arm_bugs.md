# ARM64 Native Codegen Bugs

This document tracks known bugs in the ARM64 native code generation pipeline.

---

## BUG-ARM-013: String array put passes pointer-to-value instead of value

**Status**: FIXED (2024-12-03)
**Severity**: Critical
**Symptom**: Segfault in `rt_arr_str_put` when storing strings in arrays
**Affected**: vtris (crashes immediately on startup)

### Description
When lowering string array assignments, the frontend creates an alloca, stores the string handle into it, then passes the **address of the alloca** instead of the **string value itself** to `rt_arr_str_put`.

### Root Cause (CONFIRMED)

The IL/runtime signature and frontend lowering disagree with the C runtime ABI for `rt_arr_str_put`.

- Frontend lowering currently materializes a temporary and passes its address:
  - `src/frontends/basic/RuntimeStatementLowerer.cpp:364-371`
```cpp
// ABI expects a pointer to the string handle for the value operand.
// Materialize a temporary slot, store the string handle, and pass its address.
Value tmp = lowerer_.emitAlloca(8);
lowerer_.emitStore(il::core::Type(il::core::Type::Kind::Str), tmp, value.value);
lowerer_.emitCall("rt_arr_str_put", {access.base, access.index, tmp});
```

- The runtime descriptor registers `rt_arr_str_put` with a third parameter of kind `ptr` and a VM wrapper that dereferences an extra level:
  - Signature row: `src/il/runtime/RuntimeSignatures.cpp:1055-1061` → `"void(ptr,i64,ptr)"`
  - VM wrapper: `src/il/runtime/RuntimeSignatures.cpp:468-481` reads `args[2]` as a pointer-to-rt_string and then dereferences it.

- The actual C runtime expects the string value (the handle) as the third argument, not a pointer to it:
  - `src/runtime/rt_array_str.c:77-116`
```c
void rt_arr_str_put(rt_string *arr, size_t idx, rt_string value)
```

The third argument is `rt_string` (which is `char*`), not `rt_string*`.

### IL Generated (Wrong)
```il
%t44 = alloca 8
store str, %t44, %t32    ; Store string to temp
call @rt_arr_str_put(%t35, %t36, %t44)  ; Pass &temp instead of temp contents
```

### Why VM Works
The VM path uses the descriptor wrapper to perform an extra dereference for the third argument (see `invokeRtArrStrPut`). That masks the mismatch in VM. Native codegen calls the C function directly and therefore passes the wrong thing (address-of-temporary instead of `rt_string`).

### Fix Required

Make IL/native consistent with the C runtime ABI (str-by-value):

- Change runtime signature for `rt_arr_str_put` to accept `string` for the third parameter and delete the temporary indirection in the VM wrapper.
  - Update descriptors: change `"void(ptr,i64,ptr)"` → `"void(ptr,i64,string)"` and adjust `invokeRtArrStrPut` to read `args[2]` as `rt_string` directly.
  - Update signature registry in `src/il/runtime/signatures/Signatures_Arrays.cpp` similarly.

- Update frontend lowering to pass the string handle directly:
```cpp
lowerer_.emitCall("rt_arr_str_put", {access.base, access.index, value.value});
```

This aligns VM and native paths with the C runtime and removes the undefined native behavior on AArch64.

### Validation

- Golden IL files currently show `extern @rt_arr_str_put(ptr, i64, ptr) -> void` (e.g., `src/tests/golden/basic_to_il/string_array_contains.il`). After the change they should reflect `str` for the third parameter and the frontend should stop emitting the `alloca`+`store` pattern.
- A tiny native demo that allocates a string array and stores a literal element reproduces the crash with the current code and succeeds when passing the handle directly.

---

## BUG-ARM-014: Module-level constants via rt_modvar_addr not supported in native

**Status**: FIXED (2024-12-03) - Issue was cross-block const_str lowering, not rt_modvar itself
**Severity**: Critical
**Symptom**: Cast overflow trap or garbage values when accessing CONST from included files
**Affected**: chess (crashes on COLOR statement with constants from pieces.bas)

### Description
When a BASIC program uses `CONST` values defined in an AddFile'd module, the IL uses `rt_modvar_addr_i64("CONST_NAME")` to dynamically look up the value at runtime. This works in the VM (which has a symbol table) but fails in native codegen.

### Investigation Summary

- Native runtime implementation exists, is linked by the AArch64 CLI, and works in isolation:
  - C API: `src/runtime/rt_modvar.c` returns consistent addresses per name and uses a per-VM `RtContext`.
  - AArch64 emitter initializes the runtime context in `main` (see `src/codegen/aarch64/AsmEmitter.cpp:829-846`), so native calls have an active context.
  - Minimal IL program that uses `rt_modvar_addr_i64` to increment a counter prints `1` when run via `ilc codegen arm64 -run-native`.

  Example IL (sanity-checked):
  - `extern @rt_modvar_addr_i64(str) -> ptr`
  - `global const str @.Lname = "COUNTER"`
  - Load name via `const_str`, call modvar helper, load/store i64, print → prints `1` natively.

### Likely Root Cause (Type Mismatch in Frontend Lowering)

In larger BASIC programs (e.g., chess), some CONST references lowered through `rt_modvar_addr_*` appear to pick the wrong helper/type, which then propagates a wrong-width value into `COLOR` narrowing and triggers an overflow trap.

Where this can happen:

- Helper selection depends on inferred IL type for the symbol:
  - `Lowerer::resolveVariableStorage` chooses one of `rt_modvar_addr_{i64,f64,i1,ptr,str}` based on `slotInfo.type` (`src/frontends/basic/Lowerer_Procedure.cpp:600-658`).
  - `slotInfo.type` is inferred by `inferVariableTypeForLowering` which first consults the semantic analyzer and then falls back to name suffix (`src/frontends/basic/Lowerer_Procedure.cpp:312-362`).

- If the analyzer lacks a type entry for a module-level CONST (e.g., from an AddFile’d module or due to ordering), the fallback may misclassify the name. Two failure modes are consistent with the observed trap:
  1) Selecting `rt_modvar_addr_str` for a numeric CONST, then doing `load i64, %p` reads a string handle bit-pattern as an integer, which later fails `CastSiNarrowChk` in `COLOR` (`RuntimeStatementLowerer::visit(const ColorStmt&)` narrows to 32-bit).
  2) Selecting `rt_modvar_addr_ptr` (object/pointer) for a numeric CONST and again loading as `i64` feeds a pointer-looking value to the narrowing cast.

Evidence pointing at this path:
- Narrowing-and-trap originates in `Color` lowering via `argNarrow32(...)` (`src/frontends/basic/RuntimeStatementLowerer.cpp:25-47`).
- Modvar helper selection/type inference sites are the only places where a mis-typed CONST can cause a wrong helper to be emitted, producing a pointer where an integer was expected.

### Why VM Often “Works”
The VM bridge has more forgiving marshalling (e.g., descriptors, implicit conversions) and does not rely on C ABI classification. A mis-typed modvar helper is less likely to propagate a raw pointer bit-pattern into a narrowing trap because loads/stores happen under the interpreter’s typed slots.

### Fix Required

- Strengthen type propagation for module-level CONSTs so `inferVariableTypeForLowering` always sees the semantic type across AddFile’d modules:
  - Ensure the semantic analyzer records CONST types in `varTypes_` for all module-level constants (and that lookups across files succeed) before lowering begins.
  - When the analyzer cannot provide a type, do not fall back to suffix for CONST; instead, resolve by inspecting the CONST initializer expression, which we lower anyway in `RuntimeStatementLowerer::lowerConst`.

- As a belt-and-braces guard, if the chosen helper is `*_str`/`*_ptr`, ensure subsequent loads use matching IL types rather than hard-coded `load i64`.

### Validation

- Add a focused IL/BASIC test: CONST defined in an AddFile’d module, used in `COLOR` inside a procedure. Verify the IL emits `rt_modvar_addr_i64` and a typed `load i64` and that native run matches VM.
- The minimal IL counter test confirms native `rt_modvar_addr_i64` correctness; failures correlate with type selection, not the runtime itself.
```il
%t172 = call @rt_modvar_addr_i64("CLR_WHITE")
%t173 = load i64, %t172
```

In native code, `rt_modvar_addr_i64` is not implemented correctly - it returns garbage/null, causing:
1. Loading garbage from random memory location
2. Cast checks failing because the garbage value doesn't fit in i32
3. Runtime trap with exit code 1

### Backtrace
```
rt_abort
vm_trap
rt_trap
.Ltrap_cast_1_SHOWMAINMENU  ; x11 = 0x00000001005d6740 (garbage, doesn't fit i32)
```

### Why VM Works
The VM maintains a symbol table for module variables and can look them up by name at runtime.

### Fix Required
Option A: Implement proper symbol table for native code (complex)
Option B: Inline CONST values at compile time instead of runtime lookup (preferred)
Option C: Generate static global variables for module-level constants

---

## BUG-ARM-009: Nested object field access causes bus error

**Status**: FIXED (2024-12-03) - Resolved by cross-block const_str/value lowering fixes
**Severity**: Critical
**Symptom**: Bus error (signal 10) when accessing fields of nested objects

### Description
When a class has a field that is itself an object (nested objects), accessing the nested object's fields crashes with a bus error.

### Root Cause (CONFIRMED)
The issue occurs with chained field access like `rect.topLeft.x` where `topLeft` is an object field inside `rect`.

### Minimal Reproduction
```basic
CLASS Point
    PUBLIC x AS INTEGER
END CLASS

CLASS Rectangle
    PUBLIC topLeft AS Point  ' Object field
END CLASS

DIM rect AS Rectangle
rect = NEW Rectangle()
rect.topLeft = NEW Point()
rect.topLeft.x = 10        ' BUS ERROR here
PRINT rect.topLeft.x
```

**Works**: Simple object fields, methods, object arrays
**Fails**: Object fields containing other objects (nested object access)

### Findings

- Frontend lowering for nested member stores resolves the inner field pointer and delegates to a unified scalar-assignment path that handles strings, objects (retain/release), and numerics:
  - `src/frontends/basic/RuntimeStatementLowerer.cpp:780-804` uses `resolveMemberField` then `assignScalarSlot(...)`.
  - `resolveMemberField` loads the base for nested access (`lowerMemberAccessExpr` returns the loaded pointer for an object-typed field) and computes `GEP base, offset` for the target field (`src/frontends/basic/lower/oop/Lower_OOP_Expr.cpp:406-460`).

- AArch64 GEP/Load/Store lowering is straightforward add/ldr/str and looks correct for byte offsets:
  - `src/codegen/aarch64/LowerILToMIR.cpp:816-870` (GEP → add immediate/register)
  - `src/codegen/aarch64/LowerILToMIR.cpp:886-912` (load via base+imm), `:1240+` (store via base+imm)

- Object receiver (`ME`) is materialized at method entry and loaded via a fixed stack slot:
  - `src/frontends/basic/lower/oop/Lower_OOP_Emit.cpp:114-140`

### Suspected Root Cause

An intermittent mismatch in the loaded base for chained accesses when the intermediate field is object-typed. If an inner base is not loaded (i.e., a pointer-to-pointer is used directly as an object base), the subsequent GEP targets the wrong memory and can fault. The store path and the load path share `resolveMemberField`; ensuring the base for nested member access is always the loaded object pointer (not the address of the pointer field) is critical.

This is consistent with a bus error at `rect.topLeft.x = 10` when `topLeft` is first assigned and then immediately dereferenced: a stale or mis-classified IL type (Ptr vs I64) on the intermediate load can produce an invalid base under native codegen even though the VM tolerates it.

### Next Steps

- Add a minimal BASIC/IL test that exercises: allocate `Rectangle`, assign `rect.topLeft = NEW Point()`, then set `rect.topLeft.x = 10` and print → validate both VM and native.
- When reproducing, dump the IL around the nested store; confirm that:
  - `lowerMemberAccessExpr(rect.topLeft)` produced a load of the pointer (not its address).
  - The following `GEP` uses the loaded pointer as the base.
- If a type-classification drift is found, fix it at `resolveMemberField`/`lowerMemberAccessExpr` so object-typed member access always returns the loaded pointer for use as base in further chaining.

---

## Testing Summary (Updated 2024-12-03)

| Test | Description | Result |
|------|-------------|--------|
| Print literal | `PRINT 42` | PASS |
| Variables | `x = 5; PRINT x` | PASS |
| Strings | `s = "Hello"; PRINT s` | PASS |
| IF/ELSE | Conditionals | PASS |
| FOR loop | Single loop | PASS |
| SUB | Subroutine calls | PASS |
| FUNCTION | Functions with args | PASS |
| Nested calls | `Double(AddOne(5))` | PASS |
| Recursion | Factorial | PASS |
| Simple arrays | Single element access | PASS |
| Array expressions | `arr(0) + arr(1)` | PASS |
| Array + PRINTs | PRINT before sum | PASS |
| Objects | Class instances | PASS |
| SELECT CASE | Switch statement | PASS |
| WHILE loop | While loop | PASS |
| Float literals | `x = 3.14` | PASS |
| Float simple | `x = 2.0` | PASS |
| Nested loops | FOR inside FOR | PASS |
| Sequential loops | Two FOR loops | PASS |
| Two loops + array | Write then read | PASS |
| Array of objects | Objects in array | PASS |
| MOD operation | `i MOD 2` | PASS |
| Comprehensive: data_structures | Arrays, sorting, recursion | PASS |
| Comprehensive: oop_features | Classes, methods, object arrays | PASS |
| Comprehensive: control_flow_strings | Control flow, strings | PASS |
| Native: chess | Chess game | PASS |
| Native: vtris | Tetris game | PASS |
| Native: frogger | Frogger game | PASS |

---

## Fixed Bugs

- **BUG-ARM-001**: Scratch register conflict - x9 used for both scratch and allocation (FIXED 2024-12-03)
- **BUG-ARM-002**: FP register class confusion - wrong instructions for float ops (FIXED 2024-12-03)
- **BUG-ARM-003**: Cross-block value liveness - nested loops fail (FIXED 2024-12-03)
- **BUG-ARM-004**: Sequential loops with arrays - values not stored (FIXED 2024-12-03)
- **BUG-ARM-004b**: Array of objects segfaults - same root cause as 004 (FIXED 2024-12-03)
- **BUG-ARM-005**: Entry block parameters treated as phi values (FIXED 2024-12-03)
- **BUG-ARM-006**: NullPtr not lowered (FIXED 2024-12-03)
- **BUG-ARM-007**: tempVReg lookup order for block parameters (FIXED 2024-12-03)
- **BUG-ARM-008**: Array parameters to functions - callee not in operands[0] (FIXED 2024-12-03)
- **BUG-ARM-010**: srem.chk0 (MOD) opcode not lowered (FIXED 2024-12-03)
- **BUG-ARM-011**: Cbz not allocated by register allocator (FIXED 2024-12-03)
- **BUG-ARM-012**: Cbz range limitation with .L labels (FIXED 2024-12-03 - use cmp+b.eq instead)
- **BUG-ARM-013**: String array put passes pointer-to-value instead of value (FIXED 2024-12-03)
- **BUG-ARM-014**: Module-level constants crash - was cross-block const_str issue (FIXED 2024-12-03)
- **BUG-ARM-009**: Nested object field access bus error - cross-block value issue (FIXED 2024-12-03)

---

## BUG-ARM-015: Viper.Time.SleepMs not linked for native codegen

**Status**: FIXED (2024-12-05)
**Severity**: High
**Component**: AArch64 Codegen / AsmEmitter

### Description
`Viper.Time.SleepMs` is not remapped to the C runtime function `rt_sleep_ms` in the AArch64 AsmEmitter, causing linker errors when building native executables.

### Reproduction
```basic
Viper.Time.SleepMs(100)  ' Causes linker error for native
```

### Error Message
```
Undefined symbols for architecture arm64:
  "_Viper.Time.SleepMs", referenced from:
      ...
ld: symbol(s) not found for architecture arm64
```

### Root Cause
The AsmEmitter's `mangleName()` function in `src/codegen/aarch64/AsmEmitter.cpp` doesn't include a mapping for `Viper.Time.SleepMs` to `rt_sleep_ms`.

### Fix Required
Add to AsmEmitter.cpp's mangleName function:
```cpp
if (name == "Viper.Time.SleepMs")
    return "rt_sleep_ms";
if (name == "Viper.Time.GetTickCount")
    return "rt_timer_ms";
```
