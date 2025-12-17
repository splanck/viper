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
| `ConstFold.hpp/cpp` | Constant folding for arithmetic and intrinsics |
| `DCE.hpp/cpp`       | Dead code elimination                          |
| `DSE.hpp/cpp`       | Dead store elimination                         |
| `Mem2Reg.hpp/cpp`   | Stack slot promotion to SSA                    |
| `Peephole.hpp/cpp`  | Local algebraic simplifications                |
| `EarlyCSE.hpp/cpp`  | Early common subexpression elimination         |

## Loop Passes

| File                   | Purpose                                           |
|------------------------|---------------------------------------------------|
| `LICM.hpp/cpp`         | Loop-invariant code motion                        |
| `LoopSimplify.hpp/cpp` | Loop normalization (preheaders, single backedges) |
| `SCCP.hpp/cpp`         | Sparse conditional constant propagation           |

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
| `ParamCanonicalization.hpp/cpp` | Canonicalize block parameters             |
| `ReachabilityCleanup.hpp/cpp`   | Prune unreachable blocks                  |
| `Utils.hpp/cpp`                 | Shared predicate and branch utilities     |
| `PassContext.cpp`               | Shared context for SimplifyCFG subpasses  |
