# Audit Finding #1: Backend Architecture Abstraction

## Problem
x86-64 and AArch64 backends have no shared interface. Pipeline orchestration, call lowering, frame layout, and ISel are implemented with completely different patterns.

## Implementation Plan

### Phase 1: Migrate x86-64 to PassManager (3-4 days)
AArch64 already uses the shared `codegen/common/PassManager.hpp` template. x86-64 uses a monolithic loop in `Backend.cpp:139`.

1. Define `X86Module` state struct (mirror `AArch64Module` in `passes/PassManager.hpp:35-55`):
   ```
   src/codegen/x86_64/passes/X86Module.hpp  (new)
   ```
   Fields: `ilMod`, `ti`, `mir`, `assembly`, `binaryText`, `debugLineData`

2. Extract each step from `Backend.cpp:runFunctionPipeline` into a `Pass<X86Module>`:
   - `LoweringPass` (IL→MIR + call lowering)
   - `ISelPass` (arithmetic, compare+branch, select)
   - `DivOvfPass` (lowerSignedDivRem + lowerOverflowOps)
   - `RegAllocPass`
   - `FramePass` (assignSpillSlots + insertPrologueEpilogue)
   - `PeepholePass`

3. Replace `Backend.cpp` monolithic loop with `PassManager<X86Module>` pipeline

### Phase 2: Extract Shared CallLoweringPlan (1-2 days)
x86-64 already has `CallLoweringPlan` in `CallLowering.hpp:36-74`. AArch64 has call lowering embedded in `InstrLowering.cpp:658-734`.

1. Move `CallLoweringPlan` to `codegen/common/CallLoweringPlan.hpp`
2. Add `isVarArg` field (x86 already has it; this enables Finding #4)
3. Extract AArch64 call lowering from InstrLowering.cpp into standalone `CallLowering.cpp`

### Phase 3: Unify Frame Interface (2-3 days)
1. Define abstract `FrameLayout` interface in `codegen/common/FrameLayout.hpp`:
   - `addLocal(tempId, size, align) → offset`
   - `ensureSpill(vreg, size, align) → offset`
   - `setMaxOutgoing(bytes)`
   - `finalize()`
   - `totalBytes() const`
2. AArch64's `FrameBuilder` already matches this interface — make it inherit
3. Refactor x86-64's `FrameInfo` struct + `assignSpillSlots` into a class implementing the same interface

### Files to Modify
- `src/codegen/x86_64/Backend.cpp` — decompose into passes
- `src/codegen/x86_64/Backend.hpp` — simplify to pipeline entry
- `src/codegen/x86_64/CallLowering.hpp` → `src/codegen/common/CallLoweringPlan.hpp`
- `src/codegen/x86_64/FrameLowering.hpp` — implement shared interface
- `src/codegen/aarch64/FrameBuilder.hpp` — inherit shared interface
- `src/codegen/aarch64/InstrLowering.cpp` — extract call lowering
- `src/codegen/common/FrameLayout.hpp` (new)
- `src/codegen/x86_64/passes/` (new directory)

### Verification
1. `./scripts/build_viper.sh` — all 1279 tests pass
2. Compare assembly output before/after for 5 benchmark programs — must be identical
3. Run benchmarks — no performance regression
