---
status: active
audience: contributors
last-verified: 2026-04-30
---

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
- Distinguishes address spaces: stack vs global are `NoAlias`; different globals are `NoAlias`; null aliases only
  null. `noalias` parameters are disambiguated from other pointer parameters, but not from arbitrary globals or stack
  slots.
- `typeSizeBytes` exposes conservative byte widths (i1/i16/i32/i64/f64/ptr/str/resumetok/error) for passes to thread
  sizes into `alias(...)`.
- ModRef uses a priority cascade for call effect classification:
  1. Module-level function attributes are authoritative when the callee is defined locally
  2. Runtime signature table is authoritative for known external helpers
  3. Unknown callees remain conservative; raw instruction-level `CallAttr` flags are not trusted without verified
     runtime or local function metadata
  `pure` -> `NoModRef`, `readonly` -> `Ref`, everything else -> `ModRef`.
- Runtime signature lookup uses the generated runtime table directly for known helpers and treats unknown callees as
  `ModRef`.

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

Canonical pipelines run this pass early and again after major simplification points. The `rehab-mem2reg` pipeline also
places `SimplifyCFG` around **Mem2Reg** so SSA-promotion edge arguments can be validated in isolation before cleanup.

## Mem2Reg

Promotes alloca/store/load patterns to pure SSA values:

- **Non-entry alloca promotion** (since 2026-02-17): No longer restricted to entry-block allocas. Any alloca whose
  defining block dominates all of its load/store uses is eligible for promotion. This eliminates the common pattern
  of loop variables allocated in non-entry blocks staying as load/store overhead.
- **Address-taken conservatism**: Allocas forwarded directly as branch arguments are treated as address-taken so
  promotion does not erase pointer escape information.
- **SROA (Scalar Replacement of Aggregates)**: Splits struct/record allocas into per-field allocas before promotion,
  allowing fields accessed independently to be promoted individually.
- **Sealed SSA construction**: Uses the sealed-block SSA algorithm, with deterministic alloca promotion order and sorted
  completion of incomplete block parameters.
- **Edge repair**: After promotion, mem2reg repairs branch arguments for every block parameter it introduced so partially
  populated edge argument vectors do not escape into the verifier or serializer. A literal `null` branch argument is
  preserved as data rather than treated as an unfilled repair slot.
- **Lazy dominator tree**: The dominator tree is computed only when non-entry allocas with cross-block uses are
  encountered, keeping the common (entry-block-only) case fast.
- **Fresh CFG context**: Each function is analysed with a CFG built after SROA for that function, avoiding stale edge
  state when earlier functions or SROA rewrites changed the module.
- **Tests**: `test_il_mem2reg_nonentry` — single-block alloca in non-entry block, dominating non-entry alloca with
  conditional CFG, default value before first store, and deterministic loop-header parameter/edge repair.

## Inline

- Enhanced cost model considers multiple factors beyond raw instruction count:
  - Base instruction/block budgets (defaults: `instrThreshold = 80` instructions, `blockBudget = 1` block,
    `maxInlineDepth = 3`). `blockBudget` was temporarily raised to 8 but has been reverted to 1 until open
    correctness issues in viperide and chess-zia are resolved.
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

### Threshold Changes

| Parameter | Current Default | History |
|-----------|-----------------|---------|
| `instrThreshold` | 80 | raised from 32 on 2026-02-17 to capture medium-sized helpers |
| `blockBudget` | 1 | raised to 8 on 2026-02-17, reverted to 1 after viperide/chess-zia regressions at O1 |
| `maxInlineDepth` | 3 | raised from 2 on 2026-02-17 to enable deeper utility-function chains |

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
  `x / 1 = x`, `x % 1 = 0`
- **Intentionally skipped**: `0 / x` and `0 % x` for checked division/remainder, because the divisor must
  still be evaluated for divide-by-zero traps; `x + 0.0`, because IEEE signed-zero semantics can make the
  replacement observable.
- **Float arithmetic identities**: `x * 1.0 = x`, `x / 1.0 = x`, `x - 0.0 = x`
- **Integer arithmetic identities** (on overflow-checked variants `IAddOvf`, `ISubOvf`, `IMulOvf`): `x + 0 = x`,
  `x * 1 = x`, `x - 0 = x`, `x * 0 = 0`, `x - x = 0`
- **Reflexive comparisons**: `x == x = true`, `x < x = false`, etc. for integer, signed, and unsigned comparisons
  (ICmpEq/Ne, SCmpLT/LE/GT/GE, UCmpLT/LE/GT/GE). Float reflexive comparisons are intentionally skipped because NaN
  makes `fcmp.* %x, %x` non-reflexive under IEEE-754 semantics.
- **Shift identities**: `x << 0 = x`, `x >> 0 = x`, `0 << y = 0`, `0 >> y = 0`

The pass also simplifies CBr terminators when the branch condition is a comparison of two constants:
- Float constant comparisons fold the branch to an unconditional jump
- Integer constant comparisons (signed and unsigned) fold the branch to an unconditional jump

The pass is table-driven, making it easy to add new rules without modifying core logic. Operand-forwarding rewrites are
kept within the defining block after the definition so the replacement value is available at every rewritten use.
conditions. Floating constant identity matches compare signed zero exactly, so `fsub x, +0.0 -> x` is allowed while
`fsub x, -0.0` is left intact. Full `peephole` is part of the canonical O1/O2 pipelines.

## ConstFold

Constant folding evaluates pure operations at compile time:

- **Checked integer arithmetic**: `iadd.ovf`, `isub.ovf`, `imul.ovf`, `sdiv.chk0`, `udiv.chk0`, `srem.chk0`,
  `urem.chk0` (folded when both operands are constants and the result does not trap)
- **Floating arithmetic**: `fadd`, `fsub`, `fmul`, `fdiv`
- **Bitwise**: `and`, `or`, `xor`, `shl`, `ashr`, `lshr`
- **Comparisons**: all signed, unsigned, and float comparison opcodes
- **Intrinsics**: `abs`, `ceil`, `clamp`, `cos`, `exp`, `floor`, `log`, `max`, `min`, `pow`, `sgn`, `sin`, `sqrt`, `tan`
- **Type conversions**: int/float casts with constant operands

## SCCP (Sparse Conditional Constant Propagation)

Propagates constants through the IL using sparse conditional evaluation:

- Identifies executable regions of the CFG and evaluates instructions whose operands become constant
- Folds conditional branches with known conditions; rewrites block parameters (SSA phi nodes)
- Float division by zero is not folded, keeping non-finite results out of propagated constants
- Trapping operations (SDivChk0, UDivChk0) are never folded when the divisor is zero to preserve trap semantics

## DSE (Dead Store Elimination)

Three-level dead store elimination:

1. **Intra-block DSE** (`runDSE`): Backward scan within each basic block finds stores that are overwritten before being
   read. Uses BasicAA for alias disambiguation and is conservative about calls that may modify or reference memory.
   Uses `size_t` loop counters for safe unsigned backward iteration.

2. **Cross-block DSE** (`runCrossBlockDSE`): Forward BFS analysis identifies stores to non-escaping allocas that are
   provably dead because they are overwritten on all paths before being read. Uses escape analysis for safety.
   **Limitation**: Conservatively treats any `ModRef` call as a read barrier, even for non-escaping allocas.
   Escape analysis follows `gep` and block-parameter forwarding so branch arguments cannot hide captured stack slots.

3. **MemorySSA-based DSE** (`runMemorySSADSE`): Uses the `MemorySSA` analysis (see below) to find additional dead
   stores that the BFS misses. Key precision improvement: calls are **not** treated as read barriers for non-escaping
   allocas because non-escaping stack memory is inaccessible to external calls. This eliminates stores in functions
   that call runtime helpers between writes — the dominant pattern in Zia-lowered loops.
   The non-escaping set follows `gep` chains and branch-argument propagation before treating calls as transparent.
   - Registered as `"memory-ssa"` function analysis in `PassManager`
   - Runs after `runDSE` within the `"dse"` pipeline pass
   - Tests: `src/tests/analysis/MemorySSATests.cpp` — call-barrier precision, live-store preservation,
     simple cross-block elimination, escaping alloca correctness, def/use node assignment

## LoopUnroll

Fully unrolls small constant-bound loops to reduce iteration overhead:

- Identifies counted loops with known trip counts from comparison patterns
- Configurable thresholds: max trip count (default 8), max loop size (50 instructions)
- Handles single-latch, single-exit loops with proper SSA value threading
- Clones only non-trapping, side-effect-free loop bodies with value renaming through unrolled iterations
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
  - `SDivChk0/UDivChk0/SRemChk0/URemChk0(lhs, divisor)` — demoted to the corresponding plain div/rem opcode when the
    divisor is a non-zero ConstInt and the signed `MIN / -1` trap case is impossible. The result temp is preserved.
  - **Type safety**: ConstInt values type as I64 in the verifier. When the check result type is narrower (e.g. I32 for
    IdxChk) and the result has live uses, constant elimination is skipped and the dominance-based check still applies.
    This prevents type-mismatch verifier errors when the check result is used as a narrower-typed discriminant.
- Guard-based `isub.ovf x, K` demotion requires a proof that covers the relevant overflow side. A lower-bound guard can
  demote non-negative `K`; negative constants require an upper-bound proof and remain checked.
- The verifier still rejects plain unchecked signed arithmetic by default. It accepts a demoted `sub` only when every
  incoming edge to the containing block carries the exact lower-bound proof emitted by CheckOpt; unguarded `sub`
  remains invalid IL.
- Hoists loop-invariant checks from loop headers to preheaders when the loop is EH-insensitive and operands are
  invariant, preserving trap behaviour.
- Safety rules: a check is eliminated only if the dominating check block dominates the use-site block; hoisting is
  restricted to loop headers with canonical preheaders. EH-sensitive opcodes (resume/eh push/pop) keep loops ineligible.
- Tests: `src/tests/unit/il/transform/checkopt_redundancy.cpp` covers nested-loop redundancies, non-dominating siblings (no
  elimination), constant in-bounds elimination, checked div/rem demotion, and trap-preservation cases.

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
- **Textual ordering guard**: Each available expression is stored as `AvailableExpr{value, block}`. Before
  replacing, `isTextuallyAvailable()` verifies the defining block appears at or before the use block in the
  function's block list. This prevents replacements where a dominator is textually later (e.g., entry→late→update
  where late dominates update but appears after it), which would create an illegal use-before-def in the IL.
- **Signature**: `bool runEarlyCSE(Module &M, Function &F)` — module reference needed for CFGContext construction.
- **Tests**: `test_il_earlycse_domtree` — cross-block elimination, sibling-branch non-elimination, textually-unsafe cross-block CSE rejection.

## ValueKey (CSE/GVN Support)

Expression identity keys used by EarlyCSE and GVN:

- Commutative operations with well-defined value semantics (checked integer overflow ops, And, Or, Xor, ICmpEq/Ne,
  FAdd, FMul, FCmpEQ/NE) have operands normalized by a deterministic ranking function. Plain signed Add/Sub/Mul are
  excluded because overflow policy is not encoded in the key.
- Ranking caches computed values to avoid redundant tuple construction
- `isSafeCSEOpcode` whitelist restricts CSE to pure, non-trapping operations with no memory effects
- `makeValueKey` rejects terminators, side-effecting, and memory operations

## CallEffects

Unified API for querying call instruction side effects:

- Runtime helper metadata overrides call-site hints for known helpers; the verifier rejects contradictory call
  attributes on known callees.
- Instruction-level `CallAttr` flags are metadata assertions, not an optimizer proof by themselves; unknown callees stay
  conservative.
- `canEliminateIfUnused()` requires both `pure` and `nothrow`; `canReorderWithMemory()` gates LICM and code motion.

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

## GVN (Global Value Numbering)

- Assigns value numbers to expressions using dominator-tree-scoped hash tables.
- Eliminates redundant computations including loads when no intervening store exists and the load is known
  non-trapping. Loads from unknown pointer values are not removed or reused because IL load semantics include
  null and misalignment traps.
- Uses `ValueKey` canonical forms for commutative normalization.
- Available values stored as `vector<AvailableValue>` (searched most-recent-first) per expression key,
  enabling multiple definitions of the same expression across different dominator-tree paths.
- **Textual ordering guard**: Same `isTextuallyAvailable()` check as EarlyCSE — replacements are only
  applied when the defining block appears at or before the use block in the function's block list.
  Prevents use-before-def violations when a dominator appears later textually.
- Implementation: `src/il/transform/GVN.cpp`
- **Tests**: `test_GVN` — cross-block CSE, redundant load elimination, textual order guard for load elimination.

## LICM (Loop-Invariant Code Motion)

- Hoists instructions whose operands are defined outside the loop to the loop preheader.
- Requires `LoopInfo` and `Dominators` analyses.
- Full `licm` may hoist non-trapping loads and readonly calls when BasicAA proves the loop cannot modify the observed
  memory. The `licm-safe` variant remains available as an explicit diagnostic subset that never hoists memory reads.
- O2 uses full `licm`; `rehab-licm` remains available to validate LICM independently from the broader optimizer.
- Implementation: `src/il/transform/LICM.cpp`

## EHOpt (Exception Handling Optimization)

- Removes dead exception handlers (try/catch blocks where the try body cannot throw).
- Simplifies exception handling structure after inlining.
- Implementation: `src/il/transform/EHOpt.cpp`

## LoopRotate

- Rotates loops by duplicating the header into the preheader.
- Transforms while-loops into do-while form for better analysis by LICM and IndVarSimplify.
- Implementation: `src/il/transform/LoopRotate.cpp`

## Reassociate

- Reassociates commutative/associative chains (add, mul, and/or/xor) to improve constant folding.
- Normalizes expression trees so ConstFold can fold more constants in a single pass.
- Implementation: `src/il/transform/Reassociate.cpp`

## SiblingRecursion

- Detects sibling recursive calls (tail calls to the same function) and optimizes them.
- Reduces stack growth for mutually-recursive patterns.
- Implementation: `src/il/transform/SiblingRecursion.cpp`

## Canonical Optimization Pipelines (Zia and BASIC frontends)

Before 2026-02-17, the Zia compiler (`src/frontends/zia/Compiler.cpp`) applied its own reduced pipeline
instead of the registered canonical O1/O2 pipelines:

| Level | Old (custom) | New (canonical) |
|-------|-------------|-----------------|
| O1 | 4 passes (simplify-cfg, mem2reg, peephole, dce) | Registered O1: SimplifyCFG, Mem2Reg, SCCP, ConstFold, Peephole, DCE, Inline |
| O2 | 9 passes (missing SCCP, loop passes, inline, check-opt) | Registered O2: Mem2Reg, loop shaping, full LICM, SCCP, CheckOpt, EHOpt, DCE, SiblingRecursion, Inline, Peephole, GVN, Reassociate, EarlyCSE, DSE, LateCleanup |

`mem2reg` is canonical in O1/O2 after dominance, edge-repair, non-entry alloca, and loop-reentered allocation guards made
SSA promotion verifier-clean. Full memory-hoisting `LICM` is canonical in O2 after load-safety and BasicAA mod/ref guards
made hoisting alias-conservative. Full IL `peephole` is canonical after verifier-clean local-use, use-count,
trap-preservation, and signed-zero coverage.

The pass manager verifies IR after each pass by default, including release
builds. Callers may disable this for specialised measurement, but rehab and CI
pipelines should keep per-pass verification enabled.

The BASIC frontend (`src/tools/viper/cmd_run.cpp`) only ran `SimplifyCFG` on verification failure; it now
applies the canonical O0/O1/O2 pipeline unconditionally.

**Fix**: Frontends and native codegen entry points call `pm.runPipeline(module, "O1")` / `pm.runPipeline(module, "O2")`
to use the canonically registered pipelines. The AArch64 and x86-64 native pipelines no longer maintain backend-local
O1/O2 pass lists; after native EH lowering they reject any residual structured EH opcodes before optimization, then run
the same IL pipelines as the VM/frontends.

**Test**: `test_il_canonical_pipeline` — verifies O1/O2 contain expected passes, full peephole is canonical, and SCCP
runs.

### Rehab Pipelines

The pass manager also registers targeted rehab pipelines for focused pass validation:

| Pipeline | Passes | Purpose |
|----------|--------|---------|
| `rehab-mem2reg` | `simplify-cfg, mem2reg, dce` | Isolate SSA promotion behavior for loop, non-entry alloca, and edge-repair cases |
| `rehab-peephole` | `peephole, dce` | Exercise IL peephole rewrites in isolation from the broader O1/O2 pipelines |
| `rehab-licm` | `loop-simplify, licm, simplify-cfg, dce` | Validate full loop-invariant code motion independently from the broader optimizer |

`viper il-opt --pipeline` accepts these registered names directly; lowercase rehab names are not uppercased to O-level
aliases.

Further pipeline changes should include verifier-clean IR, native-vs-VM equivalence,
and representative demo/application workload runs.

## Optimization Review Test Coverage

Regression tests covering fixes from the comprehensive IL optimization review
(`src/tests/unit/il/transform/test_opt_review_*.cpp`):

| Test File | Tests | Coverage |
|-----------|-------|---------|
| `test_opt_review_basicaa.cpp` | 7 | Priority cascade, ModRef classification, alias queries |
| `test_opt_review_calleffects.cpp` | 7 | Pure/readonly/conservative classification, generated runtime metadata priority, pure+nothrow deletion gating |
| `test_opt_review_dse.cpp` | 5 | Dead store elimination, load-intervened stores, different allocas, MemorySSA exit-block stores |
| `test_opt_review_loopinfo.cpp` | 4 | Self-loop dedup, normal loop membership, block counts |
| `test_opt_review_peephole.cpp` | 25 | UCmp/FCmp constant folding in CBr, integer reflexive comparisons, trap-preserving skipped folds, signed-zero FP identities |
| `test_opt_review_sccp.cpp` | 4 | FDiv by zero preservation, normal FDiv folding |
| `test_opt_review_valuekey.cpp` | 8 | Commutative normalization, safe opcode classification |

### Optimizer Improvement Tests (2026-02-17)

| Test File | Tests | Coverage |
|-----------|-------|---------|
| `test_il_canonical_pipeline.cpp` | 9 | O1/O2 pass registration, mem2reg and full LICM promotion, full peephole promotion, removed legacy safe alias, SCCP execution via canonical pipeline |
| `test_il_mem2reg_nonentry.cpp` | 5 | Non-entry-block alloca promotion, domination-filtered promotion, edge repair, loop-reentered alloca guard |
| `test_il_inline_threshold.cpp` | 3 | New default thresholds (80/8/3), 50-instr inline, oversized rejection |
| `test_il_earlycse_domtree.cpp` | 3 | Cross-block CSE via domtree, sibling-branch non-elimination, textually-unsafe rejection |
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
