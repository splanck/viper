# CODEMAP: IL Transform

Optimization passes (`src/il/transform/`) for IL programs.

Last updated: 2026-02-17

## Overview

- **Total source files**: 62 (.hpp/.cpp)
- **Subdirectories**: SimplifyCFG/, analysis/

## Pass Infrastructure

| File                    | Purpose                                             |
|-------------------------|-----------------------------------------------------|
| `PassManager.cpp`       | Modular pass sequencing implementation              |
| `PassManager.hpp`       | Modular pass sequencing and analysis preservation   |
| `PassRegistry.cpp`      | Pass factories implementation                       |
| `PassRegistry.hpp`      | Pass factories and default pipeline builders        |
| `PipelineExecutor.cpp`  | Pipeline execution implementation                   |
| `PipelineExecutor.hpp`  | Pipeline execution with printing/verification hooks |
| `AnalysisManager.cpp`   | On-demand analysis implementation                   |
| `AnalysisManager.hpp`   | On-demand analysis construction and caching         |

## Core Passes

| File             | Purpose                                                        |
|------------------|----------------------------------------------------------------|
| `ConstFold.cpp`  | Constant folding implementation                                |
| `ConstFold.hpp`  | Constant folding for arithmetic, bitwise ops, comparisons, and selected math intrinsics |
| `DCE.cpp`        | Dead code elimination implementation                           |
| `DCE.hpp`        | Dead code elimination                                          |
| `DSE.cpp`        | Dead store elimination implementation                          |
| `DSE.hpp`        | Dead store elimination (intra-block and cross-block)           |
| `Mem2Reg.cpp`    | Stack slot promotion implementation                            |
| `Mem2Reg.hpp`    | Stack slot promotion to SSA                                    |
| `Peephole.cpp`   | Local algebraic simplifications implementation                 |
| `Peephole.hpp`   | Local algebraic simplifications (57 rules: int/float/unsigned)  |
| `EarlyCSE.cpp`   | Early CSE implementation                                       |
| `EarlyCSE.hpp`   | Early common subexpression elimination                         |
| `GVN.cpp`        | Global value numbering implementation                          |
| `GVN.hpp`        | Global value numbering with load elimination                   |
| `Inline.cpp`     | Function inlining implementation                               |
| `Inline.hpp`     | Function inlining with enhanced cost model                     |

## Utility Passes

| File              | Purpose                                      |
|-------------------|----------------------------------------------|
| `CheckOpt.cpp`    | Optimization verification implementation     |
| `CheckOpt.hpp`    | Optimization verification and checking       |
| `LateCleanup.cpp` | Late-stage cleanup implementation            |
| `LateCleanup.hpp` | Late-stage cleanup transformations           |
| `CallEffects.hpp` | Call effect analysis with early-exit optimization |
| `ValueKey.cpp`    | Value keying with cached commutative normalization |
| `ValueKey.hpp`    | Value keying for CSE and GVN                      |

## Loop Passes

| File                  | Purpose                                                |
|-----------------------|--------------------------------------------------------|
| `LICM.cpp`            | Loop-invariant code motion implementation              |
| `LICM.hpp`            | Loop-invariant code motion (hoisting)                  |
| `LoopSimplify.cpp`    | Loop normalization implementation                      |
| `LoopSimplify.hpp`    | Loop normalization (preheaders, single backedges)      |
| `LoopUnroll.cpp`      | Loop unrolling implementation                          |
| `LoopUnroll.hpp`      | Loop unrolling for small constant-bound loops          |
| `IndVarSimplify.cpp`  | Induction variable optimization implementation         |
| `IndVarSimplify.hpp`  | Induction variable optimization and strength reduction |
| `SCCP.cpp`            | Sparse conditional constant propagation implementation |
| `SCCP.hpp`            | Sparse conditional constant propagation                |

## Analysis (`analysis/`)

| File                    | Purpose                           |
|-------------------------|-----------------------------------|
| `analysis/Liveness.cpp` | Live-in/live-out set impl         |
| `analysis/Liveness.hpp` | Live-in/live-out set computation  |
| `analysis/LoopInfo.cpp` | Loop detection implementation     |
| `analysis/LoopInfo.hpp` | Loop detection and summarization  |

## SimplifyCFG Main

| File              | Purpose                                   |
|-------------------|-------------------------------------------|
| `SimplifyCFG.cpp` | Aggregated CFG simplification impl        |
| `SimplifyCFG.hpp` | Aggregated CFG simplification entry point |

## SimplifyCFG Subpasses (`SimplifyCFG/`)

| File                                     | Purpose                                   |
|------------------------------------------|-------------------------------------------|
| `SimplifyCFG/BlockMerging.cpp`           | Collapse trivial block chains impl        |
| `SimplifyCFG/BlockMerging.hpp`           | Collapse trivial block chains             |
| `SimplifyCFG/BranchFolding.cpp`          | Simplify constant/identical branches impl |
| `SimplifyCFG/BranchFolding.hpp`          | Simplify constant/identical branches      |
| `SimplifyCFG/ForwardingElimination.cpp`  | Remove redundant forwarding blocks impl   |
| `SimplifyCFG/ForwardingElimination.hpp`  | Remove redundant forwarding blocks        |
| `SimplifyCFG/JumpThreading.cpp`          | Thread jumps implementation               |
| `SimplifyCFG/JumpThreading.hpp`          | Thread jumps through predictable branches |
| `SimplifyCFG/ParamCanonicalization.cpp`  | Canonicalize block parameters impl        |
| `SimplifyCFG/ParamCanonicalization.hpp`  | Canonicalize block parameters             |
| `SimplifyCFG/ReachabilityCleanup.cpp`    | Prune unreachable blocks impl             |
| `SimplifyCFG/ReachabilityCleanup.hpp`    | Prune unreachable blocks                  |
| `SimplifyCFG/Utils.cpp`                  | Shared utilities implementation           |
| `SimplifyCFG/Utils.hpp`                  | Shared predicate and branch utilities     |
| `SimplifyCFG/PassContext.cpp`            | Shared context for SimplifyCFG subpasses  |
