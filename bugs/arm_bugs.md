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
