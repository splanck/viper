---
status: active
audience: contributors
last-verified: 2026-06-12
---

# Backend Consolidation Plan (x86-64 ↔ AArch64)

This note tracks the remaining large consolidation items between the two
native backends. It follows a hardening pass that already landed the shared
pieces with bounded scope:

- `codegen/common/ra/CfgExtract.hpp` — one successor-extraction algorithm for
  both RA liveness analyses (fixed the x86-64 single-JCC successor bug).
- `codegen/common/PreRAForwardCopy.hpp` — one pre-RA identity-copy removal and
  single-use copy forwarding pass, traits per backend.
- Shared per-function trap blocks on AArch64 (one block per trap kind),
  matching the x86-64 design.
- Scheduler memory-dependence precision on x86-64 brought up to the AArch64
  scheduler's level (loads never conflict; disjoint 8-byte frame slots
  disambiguate).
- AArch64 argument-register allocation with clobber-aware eviction and
  marshalling reservations, mirroring the x86-64 mechanisms.

Three structural unifications remain. They are multi-week efforts and should
be done as dedicated projects, not opportunistic patches.

## C1 — Register-based block-parameter copies on AArch64 (largest win)

Today the backends implement φ/block parameters with different architectures:

- **x86-64**: critical edges are split into dedicated edge blocks carrying a
  `PX_COPY` parallel-copy pseudo; `ra/Coalescer.cpp` expands it during
  allocation with cycle breaking and spilled-operand handling. Values flow in
  registers.
- **AArch64**: every block parameter owns a stack slot; edges store
  (`PhiStoreGPR/FPR`), targets reload at entry, and every cross-block temp
  additionally round-trips its own slot per using block
  (`LowerILToMIR.cpp: allocatePhiSlots / reloadCrossBlockTempAtBlockEntry`).
  Roughly a thousand lines of post-RA peephole
  (`peephole/LoopOpt.cpp: eliminateLoopPhiSpills`,
  `forwardSinglePredPhiLoads`, `coalesceJoinPhiLoads`, plus repeated
  dead-FP-store sweeps) exist primarily to claw back this memory traffic.

**Plan:** port the x86-64 design — edge-split blocks plus a `PX_COPY`-style
pseudo lowered in the allocator — to AArch64, then delete the phi-slot
machinery and its recovery peepholes. Prerequisites already in place: the
allocator now has phys-def clobber eviction and publishes carried exit
registers (`MBasicBlock::carriedExitRegs`). The cross-block temp slots can be
retired in the same change by letting RA liveness (already CFG-aware) manage
cross-block values, as the x86-64 allocator does with its spill-home pre-pass.

Acceptance: arm64 e2e/VM-diff suites pass; loop benchmarks
(`test_codegen_arm64_benchmark_regressions`) improve or hold; frame sizes
shrink for φ-heavy functions; `eliminateLoopPhiSpills` is deleted rather than
maintained.

## C4 — One list scheduler

Both backends now have comparably precise post-RA list schedulers, but the
DAG construction, ready-queue policy, and latency tables are written twice
(`x86_64/Scheduler.cpp`, `aarch64/passes/SchedulerPass.cpp`). Extract the
shared core (dependence DAG over per-instruction use/def/flag/memory
descriptors + critical-path list scheduling) into `codegen/common`, keeping
per-target traits for: operand roles, latency model, scheduling boundaries,
and memory-disambiguation rules. The x86-64 backend should inherit the
AArch64 scheduler's SP-relative precision at that point.

## C5 — One spill-slot allocator

`x86_64/ra/Spiller.{hpp,cpp}` (slot counters + lifetime-based reuse keyed by
linear instruction indices) and `aarch64/FrameBuilder::ensureSpillWithReuse`
(block-epoch lifetime recycling) implement the same idea with different
bookkeeping and different bugs-waiting-to-happen. Unify on one slot allocator
in `codegen/common` with: per-class disjoint identifier ranges (already done
on x86-64), explicit lifetime epochs, and deterministic slot ordering. The
x86-64 placeholder-displacement encoding and the AArch64 direct-offset model
can both sit behind it.

## Known upstream issue (not codegen)

The IL optimization pipeline (`-O1`/`-O2`) can produce IL that fails
re-verification on inputs combining checked arithmetic with narrow integer
types (observed: `ret value type mismatch: expected i64 but got i16` after
optimizing a function mixing `srem.chk0`, `idx.chk`, and
`cast.fp_to_si.rte.chk`). Reproduced on unmodified baseline builds; tracked
separately from backend work.
