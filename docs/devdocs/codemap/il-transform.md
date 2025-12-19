# CODEMAP: IL Transform

Optimization passes (`src/il/transform/`) for IL programs.

## Pass Infrastructure

| File                       | Purpose                                             |
|----------------------------|-----------------------------------------------------|
| `PassManager.hpp/cpp`      | Modular pass sequencing and analysis preservation   |
| `PassRegistry.hpp/cpp`     | Pass factories and default pipeline builders        |
| `PipelineExecutor.hpp/cpp` | Pipeline execution with printing/verification hooks |
| `AnalysisManager.hpp/cpp`  | On-demand analysis construction and caching         |

## Core Passes

| File                | Purpose                                        |
|---------------------|------------------------------------------------|
| `ConstFold.hpp/cpp` | Constant folding for arithmetic, comparisons, trig/math intrinsics |
| `DCE.hpp/cpp`       | Dead code elimination                          |
| `DSE.hpp/cpp`       | Dead store elimination (intra-block and cross-block) |
| `Mem2Reg.hpp/cpp`   | Stack slot promotion to SSA                    |
| `Peephole.hpp/cpp`  | Local algebraic simplifications (57 rules including float ops) |
| `EarlyCSE.hpp/cpp`  | Early common subexpression elimination         |
| `GVN.hpp/cpp`       | Global value numbering with load elimination   |
| `Inline.hpp/cpp`    | Function inlining with enhanced cost model     |

## Loop Passes

| File                     | Purpose                                                |
|--------------------------|--------------------------------------------------------|
| `LICM.hpp/cpp`           | Loop-invariant code motion (hoisting)                  |
| `LoopSimplify.hpp/cpp`   | Loop normalization (preheaders, single backedges)      |
| `LoopUnroll.hpp/cpp`     | Loop unrolling for small constant-bound loops          |
| `IndVarSimplify.hpp/cpp` | Induction variable optimization and strength reduction |
| `SCCP.hpp/cpp`           | Sparse conditional constant propagation                |

## Analysis (`analysis/`)

| File               | Purpose                          |
|--------------------|----------------------------------|
| `Liveness.hpp/cpp` | Live-in/live-out set computation |
| `LoopInfo.hpp/cpp` | Loop detection and summarization |

## SimplifyCFG (`SimplifyCFG/`)

| File                            | Purpose                                   |
|---------------------------------|-------------------------------------------|
| `SimplifyCFG.hpp/cpp`           | Aggregated CFG simplification entry point |
| `BlockMerging.hpp/cpp`          | Collapse trivial block chains             |
| `BranchFolding.hpp/cpp`         | Simplify constant/identical branches      |
| `ForwardingElimination.hpp/cpp` | Remove redundant forwarding blocks        |
| `JumpThreading.hpp/cpp`         | Thread jumps through predictable branches |
| `ParamCanonicalization.hpp/cpp` | Canonicalize block parameters             |
| `ReachabilityCleanup.hpp/cpp`   | Prune unreachable blocks                  |
| `Utils.hpp/cpp`                 | Shared predicate and branch utilities     |
| `PassContext.cpp`               | Shared context for SimplifyCFG subpasses  |
