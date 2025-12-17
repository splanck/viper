# IL Optimization Passes

## Analysis Caching & Instrumentation

- `AnalysisManager` caches module/function analyses and drops entries after each pass based on `PreservedAnalyses`.
  Module passes must mark any preserved function analyses; otherwise function caches are cleared alongside module
  caches.
- Convenience helpers exist for common function analyses: `preserveCFG()`, `preserveDominators()`, `preserveLoopInfo()`,
  `preserveLiveness()`, and `preserveBasicAA()`.
- Enable per-pass statistics with `PassManager::setReportPassStatistics(true)` plus `setInstrumentationStream(...)` to
  receive lines like `[pass licm] bb 6 -> 6, inst 42 -> 40, analyses M:0 F:2, time 1500us`.
- Statistics track IR size (basic blocks and instructions), analysis recomputations, and wall-clock duration per pass to
  highlight redundant work.

## BasicAA (Alias/ModRef)

- Classifies pointers by base object: allocas vs parameters (including `noalias`), globals/addr_of/gaddr, const strings,
  and null.
- Follows constant-offset `gep` chains to build base+offset summaries; compares offsets with access sizes to prove
  disjoint struct/array fields when both sides have known widths.
- Distinguishes address spaces: stack vs global vs noalias param are `NoAlias`; different globals are `NoAlias`; null
  aliases only null.
- `typeSizeBytes` exposes conservative byte widths (i1/i16/i32/i64/f64/ptr/str) for passes to thread sizes into
  `alias(...)`.
- ModRef consults callee attributes plus runtime signature table; `pure` -> `NoModRef`, `readonly` -> `Ref`, everything
  else -> `ModRef`.

## SimplifyCFG

The **SimplifyCFG** pass tidies the control-flow graph before and after SSA promotion:

- Folds trivial `cbr` instructions when their condition is constant or both edges converge.
- Eliminates empty forwarding blocks that merely branch to their successor.
- Merges blocks that have a single predecessor with their unique successor when it preserves semantics.
- Prunes blocks that have become unreachable.
- Canonicalizes block parameters and the arguments supplied by branches.

### Safety Notes

SimplifyCFG deliberately skips exception-handling sensitive blocks so that landing pads and dispatch regions keep their
required structure.

### Execution Order

This pass runs once during the early pipeline, just before **Mem2Reg**, and again immediately after **Mem2Reg** to clean
up any new opportunities introduced by SSA promotion.

## Inline

- Targets tiny direct callees with simple CFGs: <=32 instructions, <=4 blocks, <=4 call sites, inline depth capped at 2.
  Skips EH-sensitive opcodes (`eh.*`, `resume.*`) and functions whose entry block takes params.
- Rewrites calls by cloning the callee CFG, threading branch arguments for block parameters, and branching returns to a
  continuation block at the call site.
- Future: consider invoke/EH awareness, entry-block parameter support, and cost-model tuning using size estimates or
  profile hints.

## LateCleanup

- End-of-pipeline polish pass: runs up to 4 iterations of `SimplifyCFG` (aggressive) followed by DCE, stopping early
  once no CFG or size changes are observed.
- Tracks IL size (blocks/instructions) before/after each iteration via an optional `LateCleanupStats` sink so pipelines
  can see how much cleanup occurred.
- Keeps work bounded; if nothing changes on an iteration, the pass exits without further scans.

## Parallel Function Passes (Experimental)

- `PassManager::enableParallelFunctionPasses(true)` allows function-local passes to run across multiple functions
  concurrently; module passes remain sequential.
- Analysis caching uses coarse locking inside `AnalysisManager` to keep computations and invalidations thread-safe;
  default remains single-threaded for determinism.
- Expect identical IR with the flag on; instrumentation order may differ slightly in parallel mode.

## Differential Testing

- `tests/unit/test_il_opt_equivalence.cpp` randomly builds small IL modules (ints/floats with `br`, `cbr`, `switch.i32`)
  using `IRBuilder`. The generator constrains constants to avoid UB (no unchecked divides; `idx.chk` uses in-range
  operands).
- Each generated module is verified, cloned, and executed on the VM before and after `O0`, `O1`, and `O2` pipelines via
  `il::transform::PassManager`. Return values and trap outcomes must match.
- Execution runs in a forked child so unexpected traps or aborts do not bring down the test harness; discrepancies print
  the seed plus the IL text for repro.
- Seeds default to a fixed constant for stability; override with `VIPER_OPT_EQ_SEED=<u64>` to fuzz locally.

## CheckOpt

- Removes redundant safety checks (bounds, div/rem-by-zero, narrowing casts) when a dominating equivalent check already
  executed on all incoming paths. Dominance is tracked with a scoped map so sibling blocks never incorrectly share
  availability.
- Hoists loop-invariant checks from loop headers to preheaders when the loop is EH-insensitive and operands are
  invariant, preserving trap behaviour.
- Safety rules: a check is eliminated only if the dominating check block dominates the use-site block; hoisting is
  restricted to loop headers with canonical preheaders. EH-sensitive opcodes (resume/eh push/pop) keep loops ineligible.
- Tests: `tests/unit/il/transform/checkopt_redundancy.cpp` covers nested-loop redundancies, non-dominating siblings (no
  elimination), and trap-preservation cases.
