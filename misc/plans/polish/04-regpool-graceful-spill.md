# POLISH-04: Graceful Register Pool Exhaustion Handling

## Context
When the AArch64 register allocator runs out of registers, it throws
`std::runtime_error` and crashes the compiler. This occurs on complex
functions with high register pressure.

**Validated throw sites** at `ra/RegPools.cpp`:
- Line 59: `takeGPR()` — "GPR pool exhausted — maybeSpillForPressure should have freed a register"
- Line 82: `takeGPRPreferCalleeSaved()` — same message
- Line 107: `takeFPR()` — "FPR pool exhausted"

**Validated:** `maybeSpillForPressure()` EXISTS at `ra/Allocator.cpp:241-255`.
It calls `selectFurthestVictim()` and `spillVictim()`. Spilling is integrated
directly into Allocator.cpp (no separate Spiller.cpp in AArch64).

**Complexity: M** | **Priority: P1**

## Design

Return sentinel `PhysReg::Invalid` instead of throwing. At call sites, retry
with emergency spill. If still exhausted, fall back to stack-only mode.

### Files to Modify

| File | Change |
|------|--------|
| `src/codegen/aarch64/ra/RegPools.cpp:59,82,107` | Return sentinel instead of throw |
| `src/codegen/aarch64/ra/RegPools.hpp` | Add `PhysReg::Invalid` sentinel |
| `src/codegen/aarch64/ra/Allocator.cpp` | Add retry logic at allocation sites |

## Documentation Updates
- `docs/release_notes/Viper_Release_Notes_0_2_4.md`
- `docs/codegen/aarch64.md`

## Verification
Stress-test function with 40+ live integer variables. Must compile correctly.
