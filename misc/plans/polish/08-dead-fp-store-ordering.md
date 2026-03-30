# POLISH-08: Fix Pass Ordering for FP Dead Store Elimination

## Context
`eliminateDeadFpStoresCrossBlock` is disabled in `Peephole.cpp`. Originally
at Pass 4.85, moved to Pass 0.7 during this session but **re-disabled** because
the ordering has a THREE-WAY dependency:

1. Must run BEFORE store-load forwarding (Pass 1.9) — forwarding hides loads
2. Must run AFTER STP merge (Pass 3+) — DSE removes stores STP needs

The per-block loop runs Passes 1-4 together. A cross-block pass can't be
sandwiched between them without restructuring.

**Complexity: M** (upgraded from S — needs pipeline restructuring) | **Priority: P2**

## Design

Split the per-block peephole loop into two phases:

```
Phase A (before cross-block DSE):
  Pass 0.9: Division strength reduction
  Pass 1: Single-instruction patterns
  Pass 2: Move folding
  Pass 3: Identity move elimination
  Pass 3+: STP/LDP merge

Cross-block pass:
  Pass 3.5: eliminateDeadFpStoresCrossBlock

Phase B (after cross-block DSE):
  Pass 4: Store-load forwarding
  Pass 4.5: Dead code elimination
```

### Files to Modify
- `src/codegen/aarch64/Peephole.cpp` — Split per-block loop into Phase A and B

## Verification
Run `test_codegen_peephole_optimizations` — all subtests must pass including
`StpMergeAdjacentStores`.
