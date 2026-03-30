# POLISH-07: Fix Cross-Block Spill Slot Reuse

## Context
**Validated location:** The disabled reuse is in `x86_64/ra/Spiller.cpp:240-243`:
```
// Spill slot reuse is disabled: the current interval analysis does not
// account for cross-block liveness, causing values still live in successor
// blocks to be silently overwritten when a slot is recycled.
```

Note: This is the x86_64 spiller. AArch64 spilling is integrated into
`ra/Allocator.cpp` via `spillVictim()` (line 177) and uses
`FrameBuilder::ensureSpillWithReuse()` (line 188).

**Validated:** `LivenessAnalysis` EXISTS in `ra/Liveness.hpp:43-82` for AArch64.
It provides `liveOutGPR(blockIdx)` and `liveOutFPR(blockIdx)` — live-out sets
per block. Live-in is NOT explicitly provided but can be computed from gen/kill.
Uses shared dataflow solver from `common/ra/DataflowLiveness.hpp`.

**Complexity: M** | **Priority: P2**

## Design

Extend spill interval tracking to cover cross-block liveness before allowing
slot sharing. Two spill slots can share only if their extended live ranges
don't overlap.

### Files to Modify

| File | Change |
|------|--------|
| `src/codegen/x86_64/ra/Spiller.cpp:240-243` | Re-enable with fixed intervals |
| `src/codegen/aarch64/ra/Allocator.cpp:188` | Verify AArch64 reuse is also safe |
| `src/codegen/common/ra/DataflowLiveness.hpp` | Ensure live-in is computable |

## Documentation Updates
- `docs/release_notes/Viper_Release_Notes_0_2_4.md`

## Verification
Compile functions with 20+ spills. Compare stack frame size before/after.
Run full test suite to verify correctness.
