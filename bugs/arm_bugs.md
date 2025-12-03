# ARM64 Native Codegen Bugs

This document tracks known bugs in the ARM64 native code generation pipeline.

---

## BUG-ARM-001: Scratch Register Conflict with Register Allocator

**Status**: FIXED (verified 2024-12-03)
**Severity**: Critical
**Symptom**: Wrong values, RT_MAGIC assertion failures, garbage output

### Description
The code generator uses `x9` (`kScratchGPR`) as a scratch register for large stack offsets (>256 bytes), but the register allocator can also allocate `x9` to user values when the register pool is exhausted.

### Root Cause
In `AsmEmitter.cpp:544-558`, when storing to stack offsets > 256 bytes:
```cpp
void AsmEmitter::emitStrToFp(std::ostream &os, PhysReg src, long long offset) const {
    if (isInSignedImmRange(offset)) {
        os << "  str " << rn(src) << ", [x29, #" << offset << "]\n";
    } else {
        // Large offset: use scratch register x9 to compute address
        emitMovRI(os, kScratchGPR, offset);  // mov x9, #offset
        os << "  add " << rn(kScratchGPR) << ", x29, " << rn(kScratchGPR) << "\n";
        os << "  str " << rn(src) << ", [" << rn(kScratchGPR) << "]\n";
    }
}
```

If `src == x9` (allocated by RegAllocLinear), then:
1. `mov x9, #offset` - loads offset into x9, destroying the value
2. `add x9, x29, x9` - computes address (now x9 is an address, not the value)
3. `str x9, [x9]` - stores x9 to itself!

In `RegAllocLinear.cpp:192-193`:
```cpp
if (gprFree.empty())
    return kScratchGPR; // Fallback to scratch register when pool exhausted
```

### Reproduction
```basic
DIM arr(5) AS INTEGER
arr(0) = 10
arr(1) = 20
arr(2) = 30
PRINT arr(0)  ' This line matters - adds PRINTs before expression
PRINT arr(0) + arr(1) + arr(2)  ' Returns garbage like 6168128600 instead of 60
```

### Fix Options
1. **Remove x9 from allocation pool**: Never allocate x9 to user values
2. **Use alternative scratch for spills**: If src==x9, use a different scratch register (x10, x16, x17)
3. **Pre-reserve x9**: Mark x9 as always unavailable in the allocator

---

## BUG-ARM-002: Floating Point Register Class Confusion

**Status**: FIXED (verified 2024-12-03)
**Severity**: Critical
**Symptom**: Assembly errors, garbage FP values

### Description
Floating point values are incorrectly handled:
1. FP literals generate invalid `fmov` instructions with non-representable immediates
2. FP values stored/loaded using GPR instructions instead of FPR instructions
3. FP arithmetic uses wrong register operands

### Root Cause
In `LowerILToMIR.cpp`, the `materializeValueToVReg` function and register handling don't properly distinguish between GPR and FPR register classes for floating point operations.

Generated assembly shows mixing:
```asm
fmov d8, #3.140000     ; ERROR: 3.14 is not a valid fmov immediate
str x8, [x29, #-16]    ; Bug: storing x8 (GPR) after loading d8 (FPR)
mov x9, #2             ; Bug: should be fmov d9, #2.0
fmul d9, d10, d11      ; Bug: d10/d11 never loaded with proper values
```

### Reproduction
```basic
DIM x AS DOUBLE
x = 3.14
PRINT x  ' Assembly error: invalid fmov immediate
```

```basic
DIM x AS DOUBLE
x = 2.0
PRINT x  ' Returns garbage: 5.47077039858234e-315
```

### Fix
1. FP constants must be loaded via data section, not immediate:
   ```asm
   adrp x9, .LC0@PAGE
   ldr d8, [x9, .LC0@PAGEOFF]
   ```
2. Track register class (GPR vs FPR) consistently through lowering
3. Use `str dN` / `ldr dN` for FPR spills, not `str xN` / `ldr xN`

---

## BUG-ARM-003: Cross-Block Value Liveness (Nested Loops Fail)

**Status**: PARTIALLY FIXED (nested loops work, but see BUG-ARM-004)
**Severity**: Critical
**Symptom**: Outer loop only runs once; values from earlier blocks become garbage

### Description
Values defined in one block and used in a later (non-successor) block are not preserved. The register allocator treats each block independently and doesn't spill values that need to survive across intervening blocks.

### Reproduction
```basic
FOR i = 1 TO 3
    FOR j = 1 TO 3
        PRINT i * 10 + j
    NEXT j
NEXT i
```
**Expected**: 11,12,13,21,22,23,31,32,33
**Actual**: 11,12,13

### Root Cause (CONFIRMED)
In the IL, `for_inc` block uses `%t2` which was defined in `UL1000000000`:
```
UL1000000000:
  store i64, %t1, 1
  %t2 = scmp_ge 1, 0          ; <-- %t2 defined here
  cbr %t2, for_head_pos, for_head_neg

for_inc:                       ; <-- Reached after inner loop completes
  %t19 = load i64, %t1
  %t20 = iadd.ovf %t19, 1
  store i64, %t1, %t20
  cbr %t2, for_head_pos, for_head_neg  ; <-- Uses %t2 from UL1000000000!
```

Generated assembly for `for_inc_main`:
```asm
for_inc_main:
  ldr x24, [x29, #-16]
  mov x25, #1
  add x26, x24, x25
  str x26, [x29, #-16]
  cmp x27, #0           ; BUG: x27 is UNINITIALIZED - should have %t2's value
  b.ne for_head_pos_main
  b for_head_neg_main
```

The value `%t2` was in register `x11` in block `UL1000000000`, but by the time `for_inc` executes (after the entire inner loop), `x11` has been reused for other purposes. The code tries to use `x27` but it was never set.

### Why This Happens
1. Register allocator processes blocks in order
2. At end of each block, vreg→phys mappings are released
3. When `for_inc` needs `%t2`, it looks for a vreg mapping that no longer exists
4. The lowering emits a comparison against whatever register was assigned (x27), which contains garbage

### Fix Options
1. **Spill long-lived values**: Detect values used across non-adjacent blocks and spill them
2. **Global liveness analysis**: Compute which values are live across block boundaries
3. **Conservative approach**: Spill all non-phi cross-block references to stack slots
4. **SSA reconstruction**: Convert cross-block uses to explicit phi parameters

---

## BUG-ARM-004: Sequential Loops with Arrays - Values Not Stored

**Status**: FIXED (verified 2024-12-03)
**Severity**: High
**Symptom**: Array values written in first loop read as 0 in second loop

### Description
When two sequential FOR loops access the same array (first writes, second reads), the values are not persisted correctly. The first loop appears to not actually store values.

### Reproduction
```basic
DIM arr(3) AS INTEGER
DIM i AS INTEGER
FOR i = 0 TO 2
    arr(i) = i * 10
NEXT i
FOR i = 0 TO 2
    PRINT arr(i)
NEXT i
```
**Expected**: 0, 10, 20
**Actual**: 0, 0, 0

### Root Cause (CONFIRMED)
This is the **same class of bug as BUG-ARM-003** - cross-block value liveness is not preserved.

In the IL:
```
UL999999998:
  store i64, %t0, 0
  %t7 = scmp_ge 1, 0          ; <-- %t7 defined here (step sign check)
  cbr %t7, for_head_pos, for_head_neg

for_body:                      ; <-- Reached via for_head_pos/neg
  %t13 = imul.ovf %t12, 10    ; <-- This gets allocated to same register as %t7!
  ...

for_inc:
  ...
  cbr %t7, for_head_pos, for_head_neg  ; <-- Uses %t7 but it's been clobbered!
```

Generated assembly shows the conflict:
```asm
UL999999998_main:
  ...
  cset x19, ge                 ; %t7 in x19

for_body_main:
  mul x23, x16, x15            ; But allocator also uses various registers...

for_inc_main:
  cmp x23, #0                  ; BUG: Comparing x23 (multiply result), not x19!
  b.ne for_head_pos_main
```

The register allocator assigned `%t7` to a register, but by the time `for_inc` is reached, that register has been reused for intermediate calculations in `for_body`. The codegen then uses whatever register was most recently assigned to the vreg, which contains garbage.

### Why BUG-ARM-003 fix didn't help
The BUG-ARM-003 fix addressed phi-elimination spill slots for **block parameters**. But `%t7` is not a block parameter - it's a regular temp that's defined in one block and used in a distant block without being passed via phi.

### Fix Required
Need to implement **global liveness analysis** to detect when a temp is:
1. Defined in block A
2. Used in block B
3. Where A and B are not directly connected (there are intervening blocks)

Such temps must be spilled to the stack at definition and reloaded at use.

### Fix Applied
Implemented **global liveness analysis** in `LowerILToMIR.cpp`:

1. **Detection**: Build map of `tempId -> defining block index`, then scan all blocks for temps used in a different block than their definition. These are "cross-block temps".

2. **Spill at definition**: After each instruction that produces a cross-block temp result, emit a store to a dedicated spill slot.

3. **Reload at use**: At block entry, for any cross-block temp used in that block (but defined elsewhere), emit a load from the spill slot into a fresh vreg.

This ensures the value is preserved in memory across block boundaries, even when the register allocator reuses physical registers.

---

## BUG-ARM-004b: Array of Objects Segfaults

**Status**: FIXED (verified 2024-12-03) - same fix as BUG-ARM-004
**Severity**: High
**Symptom**: Segmentation fault (exit code 139)

### Description
Programs using arrays of objects with two loops crash during execution.

### Reproduction
```basic
CLASS Item
    PUBLIC value AS INTEGER
END CLASS

DIM items(3) AS Item
DIM i AS INTEGER
FOR i = 0 TO 2
    items(i) = NEW Item()
    items(i).value = i * 10
NEXT i
FOR i = 0 TO 2
    PRINT items(i).value
NEXT i
```

### Root Cause
Likely the same issue as BUG-ARM-004 - the array pointer is lost between the two loops. When the second loop tries to access `items(i)`, it's reading garbage/null pointer and segfaults.

---

## BUG-ARM-005: (FIXED) Entry Block Parameters Treated as Phi Values

**Status**: Fixed
**Severity**: Critical (was causing function arguments to return garbage)

### Description
Function arguments were being loaded from uninitialized spill slots instead of ABI registers.

### Root Cause
The phi spill slot setup loop started at block index 0 (entry block), but entry block parameters are passed via ABI registers (x0, x1, ...), not via block parameter spill slots.

```cpp
// BUG: for (std::size_t bi = 0; bi < fn.blocks.size(); ++bi)
// FIX:
for (std::size_t bi = 1; bi < fn.blocks.size(); ++bi)  // Skip entry block
```

### Fix Applied
Changed loop to start at `bi = 1` to skip entry block in `LowerILToMIR.cpp`.

---

## BUG-ARM-006: (FIXED) NullPtr Not Lowered

**Status**: Fixed
**Severity**: High

### Description
`store ptr, %t0, null` crashed with RT_MAGIC assertion because NullPtr wasn't materialized.

### Fix Applied
Added NullPtr case to `materializeValueToVReg`:
```cpp
if (v.kind == il::core::Value::Kind::NullPtr) {
    outVReg = nextVRegId++;
    outCls = RegClass::GPR;
    out.instrs.push_back(
        MInstr{MOpcode::MovRI, {MOperand::vregOp(outCls, outVReg), MOperand::immOp(0)}});
    return true;
}
```

---

## BUG-ARM-007: (FIXED) tempVReg Lookup Order for Block Parameters

**Status**: Fixed
**Severity**: Critical

### Description
Block parameters were read from ABI registers instead of spill slots in non-entry blocks.

### Root Cause
In `materializeValueToVReg`, the code checked for ABI parameter registers BEFORE checking `tempVReg`. For non-entry blocks, the phi spill slot loader had already stored the value in `tempVReg`, but this was being ignored.

### Fix Applied
Reordered to check `tempVReg` first:
```cpp
if (v.kind == il::core::Value::Kind::Temp) {
    // First check if we already materialized this temp
    auto it = tempVReg.find(v.id);
    if (it != tempVReg.end()) {
        outVReg = it->second;
        // ...
        return true;
    }
    // Only then check for ABI params in entry block
    int pIdx = indexOfParam(bb, v.id);
    // ...
}
```

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
| Array + PRINTs | PRINT before sum | PASS (BUG-001 fixed) |
| Objects | Class instances | PASS |
| SELECT CASE | Switch statement | PASS |
| WHILE loop | While loop | PASS |
| Float literals | `x = 3.14` | PASS (BUG-002 fixed) |
| Float simple | `x = 2.0` | PASS (BUG-002 fixed) |
| Nested loops | FOR inside FOR | PASS (BUG-003 fixed) |
| Sequential loops | Two FOR loops | PASS |
| Two loops + array | Write then read | PASS (BUG-004 fixed) |
| Array of objects | Objects in array | PASS (BUG-004b fixed) |

---

## All Known Bugs Fixed!

All ARM64 native codegen bugs have been resolved. The test suite passes 747/747 tests.
