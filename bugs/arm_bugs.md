# Viper ARM64 Backend Bugs

This file tracks bugs discovered in the ARM64 (AArch64) native code generation backend.

## Bug Template

### BUG-ARM-XXX: [Title]
- **Status**: Open / Fixed / Won't Fix
- **Discovered**: [Date]
- **Fixed**: [Date] (if applicable)
- **Severity**: Critical / High / Medium / Low
- **Component**: [e.g., LowerILToMIR, AsmEmitter, RegAllocLinear]
- **Description**: [What happened]
- **Steps to Reproduce**: [Code snippet or steps]
- **Expected**: [What should happen]
- **Actual**: [What actually happened]
- **Root Cause**: [Technical analysis]
- **Fix**: [Description of fix applied]

---

## Open Bugs

### BUG-ARM-007: Cross-Block SSA Value Uses Cause Missing Code
- **Status**: Open
- **Severity**: Critical
- **Component**: `src/codegen/aarch64/RegAllocLinear.cpp`
- **Description**: When an SSA value defined in one block is used in another block, the destination block's instructions are not emitted.
- **Reproduction**:
  ```il
  entry:
    %val = add 10, 20
    cbr %cond, body, done
  body:
    call @print(%val)  ; NOT EMITTED - body block becomes just "b done"
  ```

**Root Cause Analysis**:
In `RegAllocLinear.cpp:250-256`, the allocator processes blocks independently:
```cpp
AllocationResult run()
{
    for (auto &bb : fn_.blocks)
    {
        allocateBlock(bb);
        releaseBlockState();  // <-- PROBLEM: releases all vreg->phys mappings!
    }
```
The `releaseBlockState()` function (line 632) releases all physical registers at block boundaries:
```cpp
void releaseBlockState()
{
    for (auto &kv : gprStates_)
    {
        if (kv.second.hasPhys)
        {
            pools_.releaseGPR(kv.second.phys);
            kv.second.hasPhys = false;
        }
    }
```
When a value from block A is used in block B, by the time block B is processed, the value's vreg has no physical register assigned and `materializeValueToVReg()` fails to find the producer instruction (it's in a different block).

**Fix Plan**:
Option 1: Before `releaseBlockState()`, spill all live-out values to stack slots and reload them as live-in values in successor blocks.
Option 2: Implement proper liveness analysis to track values that cross block boundaries.
Option 3: For values used across blocks, always go through stack slots (alloca/store/load pattern) rather than direct SSA values.

**Note**: This bug doesn't affect BASIC programs because the frontend always uses alloca/store/load for variables, avoiding direct cross-block SSA value use.

**Files to Modify**: `src/codegen/aarch64/RegAllocLinear.cpp` - add cross-block liveness handling

---

## Deep Probe: Legacy Open Issues (from /arm_bugs.md)

The repository root contains an older `arm_bugs.md` with four open issues (BUG-ARM-001..004). Below is a deep probe on their root causes with precise code references in the current tree.

### BUG-ARM-001: Scratch Register Conflict with Register Allocator
- Status: Open (legacy doc); Confirmed root cause
- Symptom: Wrong values/garbage when frame offsets exceed the signed imm range; RT_MAGIC asserts
- Root Cause (confirmed):
  - The emitter hardcodes `kScratchGPR = x9` for large-offset addressing sequences:
    - `AsmEmitter::emitLdrFromFp/emitStrToFp/emitLdrFprFromFp/emitStrFprToFp/emitLdrFromBase/emitStrToBase` all use `kScratchGPR` when `offset ∉ [-256,255]`.
    - Example: `src/codegen/aarch64/AsmEmitter.cpp` lines around 536–620 and 870–920 show: `emitMovRI(os, kScratchGPR, offset); add kScratchGPR, base, kScratchGPR; ldr/str ...`.
  - The linear allocator can also hand out `x9` when under pressure:
    - `src/codegen/aarch64/RegAllocLinear.cpp:302-326`: `RegPools::takeGPR()` returns `kScratchGPR` when `gprFree.empty()`.
    - Even though `allocateInstruction` calls `maybeSpillForPressure()` first, in edge cases with no spill candidates (e.g., all operands are defs or very early in a block), `takeGPR()` still falls back to `x9`.
  - Target includes `X9` as allocatable caller-saved reg:
    - `src/codegen/aarch64/TargetAArch64.cpp`: `info.callerSavedGPR` contains `PhysReg::X9`.
  - Collision scenario: if the value being stored/loaded resides in `x9` and the emitter needs the scratch path, the sequence `mov x9, #off; add x9, x29, x9; str x9, [x9]` clobbers the source.
- Repro (from legacy doc): Using PRINTs to grow the frame and then summing array elements can produce garbage.
- Fix directions:
  - Remove `X9` from allocatable pools (exclude from `callerSavedGPR` and `calleeSavedGPR`, or filter it in `RegPools::build`).
  - As a belt-and-suspenders, make `RegPools::takeGPR()` never return `kScratchGPR`—force an LRU spill (extend `maybeSpillForPressure` logic) and retry allocation.
  - Optional: pick a dedicated scratch not present in any pool and guard against `src == kScratchGPR` paths in emitter.

### BUG-ARM-002: Floating Point Register Class Confusion
- Status: Open (legacy doc); Confirmed multiple contributing defects
- Symptoms: Invalid `fmov dN, #imm` for general doubles, mixed GPR/FPR stores/loads, garbage FP results
- Root Causes (confirmed with code):
  1) FP constants emitted as immediates via `FMovRI` but assembler supports only a tiny constant set:
     - `LowerILToMIR::materializeValueToVReg` uses `MOpcode::FMovRI` with the 64-bit bit-pattern of the double (lines ~100–150).
     - `AsmEmitter::emitFMovRI` prints `fmov dN, #<decimal>` (lines ~680–700), which fails for values like 3.14.
     - Dispatch glue unpacks bits to `double` and calls `emitFMovRI` (`generated/OpcodeDispatch.inc`).
  2) Stores/loads of FP locals use GPR opcodes unconditionally in some paths:
     - Store case (locals): regardless of `cls` returned by `materializeValueToVReg`, the code uses `StrRegFpImm` (GPR store) instead of `StrFprFpImm` when the value class is FPR.
       - `src/codegen/aarch64/LowerILToMIR.cpp` around 2160–2220: comment “Only i64 locals for now” and unconditional `StrRegFpImm`.
     - Load case (locals): similarly unconditionally uses `LdrRegFpImm` (GPR) for results, not checking `ins.type` to choose `LdrFprFpImm`.
       - `src/codegen/aarch64/LowerILToMIR.cpp` around 2220–2280.
  3) Downstream ops expect FPRs for FP arithmetic (`FAdd/FSub/FMul/FDiv`) and comparisons, which is consistent, but the materialization path above can feed them corrupted values or assembler-invalid immediates.
- Repro (from legacy doc): `x = 3.14` triggers assembler error; `x = 2.0` prints tiny garbage.
- Fix directions:
  - Introduce an AArch64 FP literal path: place f64 in rodata and load via `adrp/add/ldr dN, [xTmp, #off]` or use an internal literal pool helper.
  - In Store/Load, branch on value/result class (or IL type) and emit `StrFprFpImm`/`LdrFprFpImm` for F64, with the correct vreg class.
  - Ensure argument passing and returns already route FP via V0–V7 and V0 (verified in `lowerCallWithArgs` and Ret path).

### BUG-ARM-003: Cross-Block Value Liveness (Nested Loops Fail)
- Status: Open (legacy doc); Confirmed allocator-level liveness gap
- Symptoms: Values defined in one block read as garbage in later blocks; nested loops break (outer loop runs once)
- Root Cause (confirmed):
  - The allocator releases all vreg→phys mappings at block ends without guaranteeing a spill for values live-out of the block:
    - `RegAllocLinear::releaseBlockState` clears `hasPhys` but does not mark `spilled` or emit a store.
  - On next-block use, `materialize` observes `st.spilled == false` and `st.hasPhys == false` and simply assigns a fresh phys reg without any reload (no prior store exists) → silent value loss.
    - See `materialize` and `handleSpilledOperand` around 349–404 in `RegAllocLinear.cpp`.
  - While `LowerILToMIR` does implement phi-through-spill for explicit block parameters, it does not cover arbitrary cross-block uses that aren’t modeled as params.
- Repro (from legacy doc): nested FOR loops; later block reuses `%t2` from a distant predecessor; allocator mapping was dropped with no spill/reload.
- Fix directions:
  - Add cross-block liveness handling to the allocator: compute live-out sets per block and spill live-outs at block end; treat live-ins similarly by reloading at block entry.
  - Or conservatively spill any vreg used in successor blocks that is not a def in the successor and not a phi/param (Phase A conservative correctness over performance).
  - Coordinate with LowerILToMIR’s param-spill scheme (don’t double-spill phi values).

### BUG-ARM-004: Array of Objects Segfaults
- Status: Open (legacy doc); Likely secondary to BUG-ARM-003
- Symptoms: Segmentation fault in programs allocating objects into arrays and iterating
 - Plausible Root Cause (supported by analysis):
  - Cross-block liveness loss (BUG-ARM-003) can corrupt loop indices or array/object pointers across loop headers and increment blocks, leading to OOB indices or invalid object addresses.
  - Given Store/Load paths for locals are GPR-only in some code paths (see BUG-ARM-002), object references as integers are fine, but FP values in mixed programs may further destabilize control/data flow.
- Fix directions:
  - Fix BUG-ARM-003 first; re-test array/object loops. If crashes persist, add targeted checks for object allocation sites and array bound checks lowering.

---

## Fixed Bugs

- **BUG-ARM-001** (2025-12-02): Control flow terminators emitted before value computations - Fixed in LowerILToMIR.cpp
- **BUG-ARM-002** (2025-12-02): Missing underscore prefix for macOS symbols - Fixed in AsmEmitter.cpp
- **BUG-ARM-003** (2025-12-02): Runtime symbol name mismatch (IL names vs C names) - Fixed via mapRuntimeSymbol()
- **BUG-ARM-009** (2025-12-03): BASIC Frontend Wrong Parameter Name in IL - Fixed in Serializer.cpp and OperandParse_ValueDetail.cpp. The IL serializer now uses `valueNames` to emit proper parameter names (e.g., `%X`) instead of opaque temp IDs (e.g., `%t0`). Also added `#` and `%` to valid identifier body characters for BASIC type suffixes.
- **BUG-ARM-006** (2025-12-03): Function Parameters Not Stored to Stack - Fixed by ARM-009. The codegen was correct; the issue was that the serializer was emitting wrong parameter references in the IL, which the parser then couldn't resolve.
- **BUG-ARM-008** (2025-12-03): OOP/Class Method Calls Crash at Runtime - Fixed by ARM-009. Class methods with implicit `Me` parameter now work correctly.
- **BUG-ARM-004** (2025-12-03): Stack Offset Exceeds ARM64 Immediate Range - Fixed in AsmEmitter.cpp. For offsets outside [-256, 255], now uses `mov x9, #offset; add x9, x29, x9; str/ldr xN, [x9]` pattern.
- **BUG-ARM-005** (2025-12-03): Duplicate Trap Labels - Fixed in LowerILToMIR.cpp. Added `trapLabelCounter` to generate unique labels per function (e.g., `.Ltrap_cast_0`, `.Ltrap_cast_1`, etc.).

---

## Investigation Notes (2025-12-02)

### What Works
- Simple PRINT statements (strings, integers)
- Integer arithmetic
- Simple IF/THEN/ELSE (when values stored in allocas)
- FOR loops (when variables are in alloca'd slots)
- String literals
- Basic control flow

### What's Broken
- Direct SSA value use across blocks (BUG-ARM-007) - low priority, doesn't affect BASIC programs

### What Was Fixed (2025-12-03)
- Functions with parameters - FIXED (BUG-ARM-009 root cause fixed)
- OOP/Classes - FIXED (BUG-ARM-006, BUG-ARM-008 fixed via ARM-009)
- Programs with many locals - FIXED (BUG-ARM-004)
- Large OOP programs with multiple casts - FIXED (BUG-ARM-005)

### Chess Demo Status
**Can now compile!** All blocking bugs have been fixed:
- ~~BUG-ARM-004: Too many locals cause invalid stack offsets~~ FIXED
- ~~BUG-ARM-005: Duplicate trap labels from casts~~ FIXED
- Note: Linking requires additional runtime symbols (Viper.Strings.FromI32, etc.) to be implemented

### Recommended Fix Priority
1. ~~**BUG-ARM-009** - FIXED~~
2. ~~**BUG-ARM-006** - FIXED~~
3. ~~**BUG-ARM-008** - FIXED~~
4. ~~**BUG-ARM-004** - FIXED~~
5. ~~**BUG-ARM-005** - FIXED~~
6. **BUG-ARM-007** - Edge case for handwritten IL, doesn't affect BASIC programs (low priority)

### Dependency Graph (Updated 2025-12-03)
```
BUG-ARM-009 (Frontend wrong param name) [FIXED]
    ├── BUG-ARM-006 (Params not stored) [FIXED]
    │       └── BUG-ARM-008 (OOP crashes) [FIXED]

BUG-ARM-004 (Stack offset range) [FIXED]
BUG-ARM-005 (Duplicate labels) [FIXED]
BUG-ARM-007 (Cross-block SSA) [OPEN, low priority]
```
