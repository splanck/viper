# POLISH-06: Fix AArch64 Loop Constant Hoisting

## Context
`hoistLoopConstants` is disabled at `Peephole.cpp:64`. The full implementation
exists in `peephole/LoopOpt.cpp:42-327+` but is buggy. It misidentifies if/else
merge points as loop headers, removing MovRI instructions from mutually exclusive
paths. Known symptoms: black ghosts in Pac-Man, crashes in Paint at -O1.

**Validated:** NO dominator analysis exists in the AArch64 codegen. The pass only
hoists `MovRI` into callee-saved GPRs (X19-X28).

**Complexity: L** | **Priority: P1**

## Design

Add lightweight dominator check: for each back-edge `B→A`, reverse BFS from B
excluding A. If entry is reachable → A doesn't dominate B → not a loop → skip.

### Files to Modify

| File | Change |
|------|--------|
| `src/codegen/aarch64/peephole/LoopOpt.cpp:102-135` | Add dominator check |
| `src/codegen/aarch64/Peephole.cpp:64` | Re-enable pass |

## Documentation Updates
- `docs/release_notes/Viper_Release_Notes_0_2_4.md`
- `docs/codegen/aarch64.md`

## Verification
Re-enable at -O1. Run Pac-Man (no black ghosts), Paint (no crashes), full test suite.
