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

- Enhanced cost model considers multiple factors beyond raw instruction count:
  - Base instruction/block budgets (configurable thresholds, default <=32 instructions, <=4 blocks)
  - Constant argument bonus: each constant arg reduces effective cost, enabling more inlining when optimization
    opportunities exist
  - Single-use function bonus: functions called only once get priority (can be DCE'd after)
  - Tiny function bonus: very small functions (<=8 instructions) inline more aggressively
  - Nested call penalty: functions with many internal calls incur code growth penalty
  - Total code growth tracking: limits module-wide instruction expansion
- Inline depth capped at 2 to prevent excessive nesting; skips EH-sensitive opcodes and recursive calls.
- Rewrites calls by cloning the callee CFG, threading branch arguments for block parameters, and branching returns to a
  continuation block at the call site.

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

## Peephole

The peephole pass applies 57 pattern-based algebraic simplifications:

- **Integer arithmetic identities**: `x + 0 = x`, `x * 1 = x`, `x - 0 = x`, `x * 0 = 0`, `x - x = 0`
- **Bitwise identities**: `x & -1 = x`, `x | 0 = x`, `x ^ 0 = x`, `x & 0 = 0`, `x ^ x = 0`, `x | x = x`
- **Shift identities**: `x << 0 = x`, `x >> 0 = x`, `0 << y = 0`, `0 >> y = 0`
- **Division identities**: `x / 1 = x`, `x % 1 = 0`, `0 / x = 0`, `0 % x = 0`
- **Reflexive comparisons**: `x == x = true`, `x < x = false`, etc. for signed, unsigned, and float
- **Float arithmetic**: `x * 1.0 = x`, `x / 1.0 = x`, `x + 0.0 = x`, `x - 0.0 = x`

The pass is table-driven, making it easy to add new rules without modifying core logic.

## ConstFold

Constant folding evaluates pure operations at compile time:

- **Arithmetic**: add, sub, mul, div, rem (signed and unsigned)
- **Bitwise**: and, or, xor, shifts
- **Comparisons**: all signed, unsigned, and float comparison opcodes
- **Intrinsics**: `sin`, `cos`, `tan`, `sqrt`, `pow`, `floor`, `ceil`, `abs`, `log`, `exp`, `min`, `max`, `clamp`, `sgn`
- **Type conversions**: int/float casts with constant operands

## DSE (Dead Store Elimination)

Two-level dead store elimination:

1. **Intra-block DSE**: Backward scan within each basic block finds stores that are overwritten before being read.
   Uses BasicAA for alias disambiguation and is conservative about calls that may modify or reference memory.

2. **Cross-block DSE**: Forward dataflow analysis identifies stores to non-escaping allocas that are provably dead
   because they are overwritten on all paths before being read. Uses escape analysis to ensure safety.

## LoopUnroll

Fully unrolls small constant-bound loops to reduce iteration overhead:

- Identifies counted loops with known trip counts from comparison patterns
- Configurable thresholds: max trip count (default 8), max loop size (50 instructions)
- Handles single-latch, single-exit loops with proper SSA value threading
- Clones loop body with value renaming through unrolled iterations
- Removes original loop blocks after unrolling

## JumpThreading (in SimplifyCFG)

Threads jumps through blocks with predictable branch conditions:

- When a predecessor passes a constant value that determines a conditional branch outcome, redirects the predecessor
  to bypass the intermediate block and jump directly to the known successor
- Eliminates unnecessary control flow and can enable further simplifications
- Runs in aggressive mode alongside other SimplifyCFG transformations

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
