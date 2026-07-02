# ADR 0026: Whole-Function Range Analysis for Checked-Arithmetic Demotion Proofs

## Status

Accepted

## Context

The IL restricts plain signed arithmetic: `add`, `sub`, `mul`, `sdiv`, `udiv`,
`srem`, and `urem` are spec-rejected opcodes that the verifier only accepts
when it can prove the operation cannot trap (no signed overflow; no divide by
zero; no `INT64_MIN / -1`). Frontends therefore emit the checked forms
(`iadd.ovf`, `sdiv.chk0`, ...) and the optimizer demotes them to plain ops
where a proof exists, with the verifier independently re-deriving the same
proof so optimized IL still verifies.

Until now both provers were block-local: facts came only from
compare-controlled incoming edges of the immediate predecessors plus a
straight-line walk of the current block (`CheckOpt` "Phase 0.6" and
`isVerifiedCheckedArithmeticDemotion` in the verifier used duplicated copies
of the same code). This missed most check-elimination opportunities in real
loops: a bound proven in the loop header does not reach a use two blocks
deeper, a masked accumulator loses its range at every join, and `idx.chk` on
a counted loop's index was never provably in-bounds.

Zia emits overflow-checked arithmetic by default, so this overhead is on the
hot path of essentially every numeric loop.

## Decision

1. **One shared analysis.** A whole-function forward value-range dataflow,
   `viper::analysis::computeIntRanges` (`src/il/analysis/IntRangeAnalysis.*`),
   computes block-entry range facts for every SSA temp: edge facts start from
   the predecessor's exit state, are refined by the branch condition on the
   edge, bind branch arguments to block params, and merge at joins by interval
   union. Loops iterate to a fixpoint with bounded widening; if the fixpoint
   is not reached within the sweep budget the analysis returns **no facts**
   (an unconverged iterate under-approximates and must not be consumed).
   Check post-conditions are part of the transfer function: after
   `idx.chk %i, lo, hi` the index is known in `[lo, hi-1]`; after a narrowing
   cast the operand is known within the target type's range.

2. **CheckOpt consumes it** (registered as the `int-ranges` function analysis)
   to: demote `iadd.ovf`/`isub.ovf`/`imul.ovf` whose operand ranges prove no
   overflow; delete `idx.chk` whose index range is provably inside constant
   bounds; and demote checked div/rem whose divisor range excludes the trap
   values (0, and for signed forms -1 unless the dividend excludes
   `INT64_MIN`).

3. **The verifier uses the same prover as a fallback.** When the block-local
   proof fails, `isVerifiedCheckedArithmeticDemotion` and a new
   `isRangeProvenDivRemDemotion` recompute the whole-function fixpoint and
   accept the plain op when the shared transfer function proves it safe. Both
   sides call the *same implementation*, so everything the optimizer demotes
   re-verifies, and the verifier accepts strictly more valid programs than
   before (no previously-accepted program is rejected).

## Invariant (unchanged)

Plain signed arithmetic and division opcodes remain rejected unless provably
trap-free. This ADR strengthens the prover; it does not weaken the rule. VM
and native backends consume identical IL, so VM/native determinism is
unaffected.

## Consequences

- Hot counted loops lose most overflow checks and in-bounds `idx.chk`s at O2,
  on both the VM and native paths.
- The verifier computes the fixpoint per plain-op instruction whose local
  proof fails (no caching in v1). The analysis is a bounded number of sweeps
  over small per-block maps; if profiling shows verification cost, a
  per-function memo threaded through the verifier is the follow-up.
- `il/verify` now links `il_analysis` (it already sat below `il/transform`,
  which links both; no layering cycle is introduced).
- The previously duplicated block-local range helpers in `CheckOpt` were
  replaced by the shared implementation; the verifier's local fast path is
  kept as the first, cheap attempt.

## Alternatives considered

- **Optimizer-side metadata ("this op was proven safe")**: rejected — the
  verifier must not trust unverifiable annotations; the proof must be
  re-derivable from the IL alone.
- **Restricting demotion to block-local proofs**: rejected — leaves most loop
  checks in place, which defeats the purpose of check elimination for the
  default (checked) arithmetic surface.
- **SCCP-integrated ranges**: rejected — entangles two lattices; CheckOpt
  already had the consumption machinery, and a standalone analysis is
  reusable by the verifier without pulling in the transform layer.
