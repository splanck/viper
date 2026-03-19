# Audit Finding #4: AArch64 ABI Completion (Varargs, PAC, BTI)

## Problem
AArch64 backend missing: variadic calling convention, Pointer Authentication, Branch Target Identification.

## Implementation Plan

### Phase A: PAC + BTI (2-3 hours, ~30 LOC)

**PAC (Pointer Authentication Code):**
1. In `A64BinaryEncoder.cpp` prologue emission (after `encodePrologue`):
   - Emit `PACIASP` (0xD503233F) before saving LR — signs return address with SP
2. In epilogue emission (before `ret`):
   - Emit `AUTIASP` (0xD50323BF) after restoring LR — verifies return address
3. Gate behind `PipelineOptions::enablePAC` flag (default true on Darwin)

**BTI (Branch Target Identification):**
1. At function entry (first instruction):
   - Emit `BTI C` (0xD503245F) — marks valid indirect call landing pad
2. Gate behind `PipelineOptions::enableBTI` flag (default true on Darwin)

**Encoding references:**
- `PACIASP`: `0xD503233F` (HINT #25)
- `AUTIASP`: `0xD50323BF` (HINT #29)
- `BTI C`:   `0xD503245F` (HINT #34)

### Phase B: Variadic Function Support (3-4 days, ~200 LOC)

1. Add `bool isVarArg{false}` to AArch64 `MFunction` struct (`MachineIR.hpp:254`)
2. Propagate from IL function metadata during lowering (`LowerILToMIR.cpp`)
3. In call lowering (currently in `InstrLowering.cpp:658-734`):
   - For variadic calls: named args use X0-X7/D0-D7 per AAPCS64
   - Variadic args (after last named param) go on stack, 8-byte aligned
   - Emit `str` to outgoing arg area for each variadic arg
4. For variadic function definition:
   - Save all argument registers to stack in prologue (va_start setup)
   - Provide mechanism to iterate saved args

**AAPCS64 variadic rules:**
- Named integer args: X0-X7, then stack
- Named FP args: D0-D7, then stack
- After last named arg, all remaining go to stack (no register allocation)
- Stack slots are 8-byte aligned (natural alignment)

### Files to Modify
- `src/codegen/aarch64/binenc/A64BinaryEncoder.cpp` — PAC/BTI in prologue/epilogue
- `src/codegen/aarch64/MachineIR.hpp` — add isVarArg to MFunction
- `src/codegen/aarch64/InstrLowering.cpp:658-734` — variadic call lowering
- `src/codegen/aarch64/LowerILToMIR.cpp` — propagate isVarArg flag
- `src/codegen/aarch64/CodegenPipeline.hpp` — add enablePAC/enableBTI to PipelineOptions

### Verification
1. `./scripts/build_viper.sh` — all tests pass
2. `objdump -d` on test binary — verify `paciasp`/`autiasp` in prologues/epilogues
3. Write test calling `printf("%d %d %d\n", 1, 2, 3)` from native AArch64 — verify correct output
4. Verify BTI: `bti c` appears at function entry
