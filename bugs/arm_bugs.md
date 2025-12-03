# ARM64 Native Codegen Bugs

This document tracks known bugs in the ARM64 native code generation pipeline.

---

## BUG-ARM-009: Nested object field access causes bus error

**Status**: Open
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

### Fix Required
Investigation needed in GEP (GetElementPointer) lowering for chained object field access.

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
| Comprehensive: data_structures | Arrays, sorting, recursion | FAIL (bus error) |
| Comprehensive: oop_features | Classes, methods, object arrays | FAIL (BUG-ARM-009) |
| Comprehensive: control_flow_strings | Control flow, strings | FAIL (bus error) |
| Native: chess | Chess game | PASS |
| Native: vtris | Tetris game | PASS |

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
