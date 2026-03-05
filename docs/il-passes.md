# IL Optimization Passes

## Analysis Caching & Instrumentation

- `AnalysisManager` caches module/function analyses and drops entries after each pass based on `PreservedAnalyses`.
  Module passes must mark any preserved function analyses; otherwise function caches are cleared alongside module
  caches.
- Convenience helpers exist for common function analyses: `preserveBasicAA()`, `preserveCFG()`, `preserveDominators()`,
  `preserveLiveness()`, and `preserveLoopInfo()`.
- Enable per-pass statistics with `PassManager::setReportPassStatistics(true)` plus `setInstrumentationStream(...)` to
  receive lines like `[pass licm] bb 6 -> 6, inst 42 -> 40, analyses M:0 F:2, time 1500us`.
- Statistics track IR size (basic blocks and instructions), analysis recomputations, and wall-clock duration per pass to
  highlight redundant work.

## Post-Dominator Tree Analysis (`"post-dominators"`)

- Computes the **post-dominator tree** for each function: block X post-dominates block Y iff every path from Y to
  any function exit passes through X.
- **Algorithm**: Cooper–Harvey–Kennedy iterative dataflow on the reversed CFG. Exit blocks (those with no CFG
  successors) are the roots; a virtual exit node represented by `nullptr` connects them all.
- **Registration**: Available as analysis `"post-dominators"` in `PassManager` — query via
  `analysis.getFunctionResult<PostDomTree>("post-dominators", fn)`.
- **API**: `postDominates(A, B)` — true iff A is an ancestor of B in the post-dominator tree; `immediatePostDominator(B)` —
  immediate parent in the tree (`nullptr` for exit blocks, indicating the virtual exit).
- **Multiple exits**: When a function has two paths to separate exit blocks with no common block, the entry's
  immediate post-dominator is `nullptr` (virtual exit) — no concrete block post-dominates the entry.
- **Implementation**: `src/il/analysis/Dominators.hpp/.cpp`
- **Tests**: `src/tests/analysis/PostDominatorsTests.cpp` — linear chain, diamond (if/else join), and
  multiple-exit cases.

## BasicAA (Alias/ModRef)

- Classifies pointers by base object: allocas vs parameters (including `noalias`), globals (via `AddrOf`/`GAddr`
  opcodes), const strings, and null.
- Follows constant-offset `gep` chains to build base+offset summaries; compares offsets with access sizes to prove
  disjoint struct/array fields when both sides have known widths.
- Distinguishes address spaces: stack vs global vs noalias param are `NoAlias`; different globals are `NoAlias`; null
  aliases only null.
- `typeSizeBytes` exposes conservative byte widths (i1/i16/i32/i64/f64/ptr/str) for passes to thread sizes into
  `alias(...)`.
- ModRef uses a priority cascade for call effect classification:
  1. Instruction-level `CallAttr` flags (pure, readonly) are checked first
  2. Module-level function attributes are authoritative when the callee is defined locally
  3. Runtime signature table is consulted only for external (non-module) functions
  `pure` -> `NoModRef`, `readonly` -> `Ref`, everything else -> `ModRef`.
- Runtime signature lookup uses a cached hash map for O(1) amortized lookups, rebuilt automatically when new signatures
  are registered.

## SimplifyCFG

The **SimplifyCFG** pass tidies the control-flow graph before and after SSA promotion:

- Canonicalizes block parameters and the arguments supplied by branches.
- Eliminates empty forwarding blocks that merely branch to their successor.
- Folds trivial `cbr` instructions when their condition is constant or both edges converge.
- Merges blocks that have a single predecessor with their unique successor when it preserves semantics.
- Prunes blocks that have become unreachable.

### Safety Notes

SimplifyCFG deliberately skips exception-handling sensitive blocks so that landing pads and dispatch regions keep their
required structure.

### Execution Order

This pass runs once during the early pipeline, just before **Mem2Reg**, and again immediately after **Mem2Reg** to clean
up any new opportunities introduced by SSA promotion.

## Mem2Reg

Promotes alloca/store/load patterns to pure SSA values:

- **Non-entry alloca promotion** (since 2026-02-17): No longer restricted to entry-block allocas. Any alloca whose
  defining block dominates all of its load/store uses is eligible for promotion. This eliminates the common pattern
  of loop variables allocated in non-entry blocks staying as load/store overhead.
- **SROA (Scalar Replacement of Aggregates)**: Splits struct/record allocas into per-field allocas before promotion,
  allowing fields accessed independently to be promoted individually.
- **Sealed SSA construction**: Builds complete SSA form in a single pass via iterated dominance frontier computation.
- **Lazy dominator tree**: The dominator tree is computed only when non-entry allocas with cross-block uses are
  encountered, keeping the common (entry-block-only) case fast.
- **Tests**: `test_il_mem2reg_nonentry` — single-block alloca in non-entry block, dominating non-entry alloca with
  conditional CFG.

## Inline

- Enhanced cost model considers multiple factors beyond raw instruction count:
  - Base instruction/block budgets (configurable thresholds, default ≤80 instructions, ≤8 blocks)
  - Constant argument bonus: each constant arg reduces effective cost, enabling more inlining when optimization
    opportunities exist
  - Nested call penalty: functions with many internal calls incur code growth penalty
  - Single-use function bonus: functions called only once get priority (can be DCE'd after)
  - Tiny function bonus: very small functions (≤8 instructions) inline more aggressively
  - Total code growth tracking: limits module-wide instruction expansion
- Inline depth capped at 3 to allow multi-level utility-function chains to collapse; skips EH-sensitive opcodes and
  recursive calls.
- Rewrites calls by cloning the callee CFG, threading branch arguments for block parameters, and branching returns to a
  continuation block at the call site.

### Threshold Changes (2026-02-17)

| Parameter | Old Value | New Value | Rationale |
|-----------|-----------|-----------|-----------|
| `instrThreshold` | 32 | 80 | Captures medium-sized helpers that previously stayed as call overhead |
| `blockBudget` | 4 | 8 | Allows inlining functions with conditional branches |
| `maxInlineDepth` | 2 | 3 | Enables deeper utility-function chains to collapse |

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

- `src/tests/unit/test_il_opt_equivalence.cpp` randomly builds small IL modules (ints/floats with `br`, `cbr`, `switch.i32`)
  using `IRBuilder`. The generator constrains constants to avoid UB (no unchecked divides; `idx.chk` uses in-range
  operands).
- Each generated module is verified, cloned, and executed on the VM before and after `O0`, `O1`, and `O2` pipelines via
  `il::transform::PassManager`. Return values and trap outcomes must match.
- Execution runs in a forked child so unexpected traps or aborts do not bring down the test harness; discrepancies print
  the seed plus the IL text for repro.
- Seeds default to a fixed constant for stability; override with `VIPER_OPT_EQ_SEED=<u64>` to fuzz locally.

## Peephole

The peephole pass applies 57 pattern-based algebraic simplifications:

- **Bitwise identities**: `x & -1 = x`, `x | 0 = x`, `x ^ 0 = x`, `x & 0 = 0`, `x ^ x = 0`, `x | x = x`
- **Division identities** (on div-by-zero–checked variants `SDivChk0`, `UDivChk0`, `SRemChk0`, `URemChk0`):
  `x / 1 = x`, `x % 1 = 0`, `0 / x = 0`, `0 % x = 0`
- **Float arithmetic identities**: `x * 1.0 = x`, `x / 1.0 = x`, `x + 0.0 = x`, `x - 0.0 = x`
- **Integer arithmetic identities** (on overflow-checked variants `IAddOvf`, `ISubOvf`, `IMulOvf`): `x + 0 = x`,
  `x * 1 = x`, `x - 0 = x`, `x * 0 = 0`, `x - x = 0`
- **Reflexive comparisons**: `x == x = true`, `x < x = false`, etc. for signed, unsigned, and float comparisons
  (ICmpEq/Ne, SCmpLT/LE/GT/GE, UCmpLT/LE/GT/GE, FCmpEQ/NE/LT/LE/GT/GE)
- **Shift identities**: `x << 0 = x`, `x >> 0 = x`, `0 << y = 0`, `0 >> y = 0`

The pass also simplifies CBr terminators when the branch condition is a comparison of two constants:
- Float constant comparisons fold the branch to an unconditional jump
- Integer constant comparisons (signed and unsigned) fold the branch to an unconditional jump

The pass is table-driven, making it easy to add new rules without modifying core logic.

## ConstFold

Constant folding evaluates pure operations at compile time:

- **Arithmetic**: add, div, mul, rem, sub (signed and unsigned)
- **Bitwise**: and, or, shifts, xor
- **Comparisons**: all signed, unsigned, and float comparison opcodes
- **Intrinsics**: `abs`, `ceil`, `clamp`, `cos`, `exp`, `floor`, `log`, `max`, `min`, `pow`, `sgn`, `sin`, `sqrt`, `tan`
- **Type conversions**: int/float casts with constant operands

## SCCP (Sparse Conditional Constant Propagation)

Propagates constants through the IL using sparse conditional evaluation:

- Identifies executable regions of the CFG and evaluates instructions whose operands become constant
- Folds conditional branches with known conditions; rewrites block parameters (SSA phi nodes)
- Float division by zero is folded to IEEE 754 infinity/NaN rather than left as overdefined, enabling
  further optimization of code paths that depend on the result
- Trapping operations (SDivChk0, UDivChk0) are never folded when the divisor is zero to preserve trap semantics

## DSE (Dead Store Elimination)

Three-level dead store elimination:

1. **Intra-block DSE** (`runDSE`): Backward scan within each basic block finds stores that are overwritten before being
   read. Uses BasicAA for alias disambiguation and is conservative about calls that may modify or reference memory.
   Uses `size_t` loop counters for safe unsigned backward iteration.

2. **Cross-block DSE** (`runCrossBlockDSE`): Forward BFS analysis identifies stores to non-escaping allocas that are
   provably dead because they are overwritten on all paths before being read. Uses escape analysis for safety.
   **Limitation**: Conservatively treats any `ModRef` call as a read barrier, even for non-escaping allocas.

3. **MemorySSA-based DSE** (`runMemorySSADSE`): Uses the `MemorySSA` analysis (see below) to find additional dead
   stores that the BFS misses. Key precision improvement: calls are **not** treated as read barriers for non-escaping
   allocas because non-escaping stack memory is inaccessible to external calls. This eliminates stores in functions
   that call runtime helpers between writes — the dominant pattern in Zia-lowered loops.
   - Registered as `"memory-ssa"` function analysis in `PassManager`
   - Runs after `runDSE` within the `"dse"` pipeline pass
   - Tests: `src/tests/analysis/MemorySSATests.cpp` — call-barrier precision, live-store preservation,
     simple cross-block elimination, escaping alloca correctness, def/use node assignment

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
- Null-safe: target block lookups guard against missing labels to avoid crashes on malformed CFG

## CheckOpt

- Removes redundant safety checks (bounds, div/rem-by-zero, narrowing casts) when a dominating equivalent check already
  executed on all incoming paths. Dominance is tracked with a scoped map so sibling blocks never incorrectly share
  availability.
- **Constant-operand elimination (SCCP integration)**: When SCCP's `rewriteConstants()` has inlined operands as literal
  `ConstInt` values before CheckOpt runs, the pass evaluates check conditions statically at compile time:
  - `IdxChk(index, lo, hi)` — eliminated when all three are ConstInt and `lo <= index < hi`
  - `SDivChk0/UDivChk0/SRemChk0/URemChk0(lhs, divisor)` — eliminated when divisor is a non-zero ConstInt
  - **Type safety**: ConstInt values type as I64 in the verifier. When the check result type is narrower (e.g. I32 for
    IdxChk) and the result has live uses, constant elimination is skipped and the dominance-based check still applies.
    This prevents type-mismatch verifier errors when the check result is used as a narrower-typed discriminant.
- Hoists loop-invariant checks from loop headers to preheaders when the loop is EH-insensitive and operands are
  invariant, preserving trap behaviour.
- Safety rules: a check is eliminated only if the dominating check block dominates the use-site block; hoisting is
  restricted to loop headers with canonical preheaders. EH-sensitive opcodes (resume/eh push/pop) keep loops ineligible.
- Tests: `src/tests/unit/il/transform/checkopt_redundancy.cpp` covers nested-loop redundancies, non-dominating siblings (no
  elimination), constant in-bounds elimination, constant non-zero divisor elimination, and trap-preservation cases.

## EarlyCSE

Dominator-tree-scoped common subexpression elimination:

- **Scope model**: Maintains a stack of expression hash tables (one per domtree level) during a pre-order DFS
  over the dominator tree. An expression computed in block A is visible in every block A dominates because A's
  table remains on the stack while its dominated subtree is processed.
- **Cross-block CSE**: Redundant pure expressions in dominated successors are eliminated — not just within the same
  block. For example, `add a, b` in an entry block eliminates a commuted `add b, a` in its only successor.
- **Sibling isolation**: When the DFS leaves a block, its table is popped, so expressions in one branch of a
  conditional cannot eliminate the same expression in a sibling branch (which would be incorrect).
- **Commutative normalization**: Uses the same `ValueKey` canonical form as GVN so `add a,b` and `add b,a`
  share a single table entry.
- **Pure ops only**: Only instructions passing `makeValueKey` are eligible (no memory ops, calls, or terminators).
- **Signature**: `bool runEarlyCSE(Module &M, Function &F)` — module reference needed for CFGContext construction.
- **Tests**: `test_il_earlycse_domtree` — cross-block elimination, sibling-branch non-elimination.

## ValueKey (CSE/GVN Support)

Expression identity keys used by EarlyCSE and GVN:

- Commutative operations (Add, Mul, And, Or, Xor, ICmpEq/Ne, FAdd, FMul, FCmpEQ/NE) have operands
  normalized by a deterministic ranking function so `add a,b` and `add b,a` produce the same key
- Ranking caches computed values to avoid redundant tuple construction
- `isSafeCSEOpcode` whitelist restricts CSE to pure, non-trapping operations with no memory effects
- `makeValueKey` rejects terminators, side-effecting, and memory operations

## CallEffects

Unified API for querying call instruction side effects:

- Combines instruction-level `CallAttr` flags, `HelperEffects` constexpr table, and runtime signature registry
- Short-circuits: when all flags (pure, readonly, nothrow) are already classified, skips the O(n) registry scan
- `canEliminateIfUnused()` gates DCE; `canReorderWithMemory()` gates LICM and code motion

## IndVarSimplify

Induction variable optimization and strength reduction:

- Null-safe: guards against deleted instructions when looking up address expressions

## LoopInfo

Loop detection and nesting analysis:

- blockLabels vector contains no duplicates (important for self-loops where latch == header)
- Members hash set provides O(1) `contains()` queries
- Latch labels tracked separately from body membership

## CallGraph / SCC

Direct-call graph with strongly connected component analysis:

- **Edge building**: Scans all functions for `Call` instructions with non-empty `callee` field; records
  caller→callee edges and per-callee call counts. Indirect calls and unresolved targets are ignored.
- **SCC computation** (since 2026-02-17): Tarjan's iterative SCC algorithm runs after edge collection to
  identify mutually recursive function groups. SCCs are stored in reverse topological order (callees before
  callers) in `CallGraph::sccs`. Each function name maps to its SCC index via `CallGraph::sccIndex`.
- **`isRecursive(fn)`**: Returns true when `fn` is in a multi-member SCC or has a self-edge.
- **Use cases**: Bottom-up interprocedural analysis (process leaf SCCs first); inliner can skip recursive
  SCCs more accurately than the previous per-edge self-loop check.
- **Tests**: `test_il_callgraph_scc` — linear chain ordering, mutual recursion, self-recursion, `isRecursive`.

## Canonical Optimization Pipelines (Zia and BASIC frontends)

Before 2026-02-17, the Zia compiler (`src/frontends/zia/Compiler.cpp`) applied its own reduced pipeline
instead of the registered canonical O1/O2 pipelines:

| Level | Old (custom) | New (canonical) |
|-------|-------------|-----------------|
| O1 | 4 passes (simplify-cfg, mem2reg, peephole, dce) | 10 passes including SCCP, LICM |
| O2 | 9 passes (missing SCCP, loop passes, inline, check-opt) | 20+ passes — full registered pipeline |

The BASIC frontend (`src/tools/viper/cmd_run.cpp`) only ran `SimplifyCFG` on verification failure; it now
applies the canonical O0/O1/O2 pipeline unconditionally.

**Fix**: Both frontends now call `pm.runPipeline(module, "O1")` / `pm.runPipeline(module, "O2")` to use
the canonically registered pipelines, ensuring VM-interpreted programs receive the same optimization as
natively compiled ones.

**Test**: `test_il_canonical_pipeline` — verifies O1/O2 contain expected passes and that SCCP runs.

## Optimization Review Test Coverage

Regression tests covering fixes from the comprehensive IL optimization review
(`src/tests/unit/il/transform/test_opt_review_*.cpp`):

| Test File | Tests | Coverage |
|-----------|-------|---------|
| `test_opt_review_basicaa.cpp` | 7 | Priority cascade, ModRef classification, alias queries |
| `test_opt_review_calleffects.cpp` | 5 | Pure/readonly/conservative classification, by-name lookup |
| `test_opt_review_dse.cpp` | 4 | Dead store elimination, load-intervened stores, different allocas |
| `test_opt_review_loopinfo.cpp` | 4 | Self-loop dedup, normal loop membership, block counts |
| `test_opt_review_peephole.cpp` | 20 | UCmp/FCmp constant folding in CBr, reflexive comparisons |
| `test_opt_review_sccp.cpp` | 4 | FDiv by zero → infinity/NaN, normal FDiv folding |
| `test_opt_review_valuekey.cpp` | 8 | Commutative normalization, safe opcode classification |

### Optimizer Improvement Tests (2026-02-17)

| Test File | Tests | Coverage |
|-----------|-------|---------|
| `test_il_canonical_pipeline.cpp` | 4 | O1/O2 pass registration, SCCP execution via canonical pipeline |
| `test_il_mem2reg_nonentry.cpp` | 2 | Non-entry-block alloca promotion, domination-filtered promotion |
| `test_il_inline_threshold.cpp` | 3 | New default thresholds (80/8/3), 50-instr inline, oversized rejection |
| `test_il_earlycse_domtree.cpp` | 2 | Cross-block CSE via domtree, sibling-branch non-elimination |
| `test_il_callgraph_scc.cpp` | 4 | Linear chain SCC ordering, mutual recursion, self-recursion, isRecursive |

### SCCP ↔ CheckOpt Integration (2026-02-17)

Added constant-operand check elimination in CheckOpt so that checks whose operands are already inlined as
`ConstInt` literals (by SCCP's `rewriteConstants()`) are statically evaluated:

- `IdxChk(5, 0, 10)` with I64 result → eliminated (index 5 is trivially in [0, 10))
- `SDivChk0(12, 3)` with I64 result → eliminated (divisor 3 ≠ 0)
- Narrower-typed checks (I32 IdxChk result) with live uses are preserved to avoid IL verifier type mismatches
  (ConstInt is always typed I64; substituting into an I32 scrutinee position would fail verification)

| Test File | Tests | Coverage |
|-----------|-------|---------|
| `checkopt_redundancy.cpp` | 3 new | Const in-bounds IdxChk elimination, non-zero SDivChk0, OOB preservation |

### Post-Dominator Tree (2026-02-17)

`computePostDominatorTree()` added to `src/il/analysis/Dominators.hpp/.cpp`, registered as `"post-dominators"`
analysis in `PassManager`. Enables future aggressive DCE (control-dependent elimination) and precise
speculative hoisting.

| Test File | Tests | Coverage |
|-----------|-------|---------|
| `PostDominatorsTests.cpp` | 3 | Linear chain, diamond join (merge pdoms entry), multiple exits |

### SimplifyCFG Self-Loop Fix (2026-02-17)

**Bug**: `mergeSinglePred` in `BlockMerging.cpp` excluded self-loop edges from predecessor counting
(`if (&candidate == &block) continue`). A loop body with a self-loop appeared to have only 1 external
predecessor, causing it to be incorrectly merged into the entry block. The label rewrite then turned the
self-loop into a back-edge to entry, creating an infinite alloca-stack loop.

**Fix**: Count self-loop edges in the predecessor total but only record non-self blocks as the merge candidate.
The guard `predecessorEdges == 1` now correctly rejects merge when the block has both an external predecessor
and a self-loop.

**Affected tests**: `test_basic_do_exit` (DO WHILE loop back-edge corruption), `test_il_opt_equivalence`
(random differential test for optimizer pipelines).
