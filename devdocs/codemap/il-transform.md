# CODEMAP: IL Transform

- **src/il/transform/ConstFold.cpp**

  Runs the IL constant-folding pass, replacing integer arithmetic and recognised runtime math intrinsics with precomputed values. Helpers such as `wrapAdd`/`wrapMul` model modulo 2^64 behaviour so folded results mirror VM semantics, and `foldCall` maps literal arguments onto runtime helpers like `rt_abs`, `rt_floor`, and `rt_pow_f64_chkdom`. The pass walks every function, substitutes the folded value via `replaceAll`, and erases the defining instruction in place to keep blocks minimal while respecting domain checks. Dependencies include `il/transform/ConstFold.hpp`, IL core containers (`Module`, `Function`, `Instr`, `Value`), and the standard `<cmath>`, `<cstdint>`, `<cstdlib>`, and `<limits>` headers.

- **src/il/transform/DCE.cpp**

  Houses the trivial dead-code elimination pass that prunes unused temporaries, redundant memory instructions, and stale block parameters. It tallies SSA uses across instructions, erases loads, stores, and allocas whose results never feed later consumers, and mirrors a lightweight liveness sweep. A final walk drops unused block parameters and rewrites branch argument lists to keep control flow well-formed. The implementation leans on `il/transform/DCE.hpp`, IL core structures (`Module`, `Function`, `Instr`, `Value`), and standard `<unordered_map>` and `<unordered_set>` containers.

- **src/il/transform/DCE.hpp**

  Declares the front door for the dead-code elimination pass invoked by the optimizer. It exposes a single `dce` function that mutates an `il::core::Module` in place so driver code can simplify programs before deeper analyses. Dependencies are restricted to the IL forward declarations in `il/core/fwd.hpp`.

- **src/il/transform/Mem2Reg.cpp**

  Implements the sealed mem2reg algorithm that promotes stack slots introduced by `alloca` into SSA block parameters. The pass gathers allocation metadata, tracks reaching definitions per block, and patches branch arguments to thread promoted values through the CFG. It also maintains statistics about eliminated loads/stores and rewrites instructions in place so later passes see SSA form without detours through memory. Dependencies include `il/transform/Mem2Reg.hpp`, `il/analysis/CFG.hpp`, IL core types (`Function`, `BasicBlock`, `Instr`, `Value`, `Type`), and standard containers such as `<unordered_map>`, `<unordered_set>`, `<queue>`, `<optional>`, `<algorithm>`, and `<functional>`.

- **src/il/transform/Mem2Reg.hpp**

  Declares the public entry point for the mem2reg optimization along with an optional statistics structure. Clients provide an `il::core::Module` and receive the number of promoted variables and eliminated memory operations when they pass a `Mem2RegStats` pointer. The interface is used by the optimizer driver and test harnesses to promote locals before other analyses run. Dependencies include `il/core/Module.hpp`.

- **src/il/transform/Peephole.cpp**

  Implements local IL peephole optimizations that simplify algebraic identities and collapse conditional branches. Constant-detection helpers and use counters ensure SSA safety before forwarding operands or rewriting branch terminators into unconditional jumps. Rewrites also tidy `brArgs` bundles and delete single-use predicate definitions so subsequent passes see canonical control flow. Dependencies include `il/transform/Peephole.hpp`, IL core structures (`Module`, `Function`, `Instr`, `Value`), and the standard containers brought in by that header.

- **src/il/transform/Peephole.hpp**

  Declares the peephole pass entry point corresponding to the implementation described above.

- **src/il/transform/PassManager.cpp**

  Hosts the modular pass manager that sequences module/function passes, wraps callbacks, and tracks analysis preservation across runs. It synthesizes CFG and liveness information to support passes, instantiates adapters that expose pass identifiers, and invalidates cached analyses when a pass does not declare them preserved. The implementation also provides helper factories for module/function pass lambdas and utilities to mark entire analysis sets as kept or dropped. Key dependencies span the pass manager headers, IL analysis utilities (`CFG`, `Dominators`, liveness builders), IL core containers, the verifier, and standard unordered containers.

- **src/il/transform/PassManager.hpp**

  Declares a small, composable pass manager API that sequences named passes over modules and functions. It provides registration points, pipeline execution, and hooks for printing and verification, shared across optimizer entry points and tools.

- **src/il/transform/PassRegistry.hpp**

  Declares the registry and factories for built-in IL passes, exposing stable identifiers and helpers to assemble default pipelines. Keeps pass discovery centralized so tools and drivers can select passes by name.

- **src/il/transform/PipelineExecutor.hpp**

  Declares helpers that run pre-registered pass pipelines with optional analysis preservation and printing hooks. Provides convenience wrappers to execute common pipelines without manual pass manager wiring.

- **src/il/transform/analysis/Liveness.hpp**

  Declares liveness analysis utilities used by optimization passes to compute live-in/live-out sets and guide transformations. Complements CFG and dominator analyses and feeds into allocations or simplifications.

- **src/il/transform/analysis/LoopInfo.hpp**

- **src/il/transform/LICM.cpp**, **src/il/transform/LICM.hpp**

  Loop‑invariant code motion pass that hoists instructions with no side‑effects out of innermost loops using loop info and dominator queries. Avoids moving across potential traps or runtime calls.

- **src/il/transform/LoopSimplify.cpp**, **src/il/transform/LoopSimplify.hpp**

  Normalises loop shapes (preheaders, single backedges) to enable LICM and other passes. Rewrites branches and block parameters to ensure canonical form.

- **src/il/transform/SCCP.cpp**, **src/il/transform/SCCP.hpp**

  Sparse conditional constant propagation that merges constant facts along feasible paths, folds instructions, and prunes unreachable blocks.

- **src/il/transform/AnalysisManager.cpp**, **src/il/transform/AnalysisManager.hpp**

  Coordinates on‑demand analysis construction (CFG, dominators, liveness, loop info) and caching/preservation across passes.

- **src/il/transform/PipelineExecutor.cpp**

  Implements pipeline execution helpers declared in the header, wiring pass manager and preservation/printing hooks.

- **src/il/transform/PassRegistry.cpp**

  Implements pass registration and default pipeline builders declared in the header.

- **src/il/transform/SimplifyCFG/BlockMerging.cpp**, **BlockMerging.hpp**

  Collapses trivial block chains by merging blocks when successors/predecessors align and parameter/argument lists match.

- **src/il/transform/SimplifyCFG/BranchFolding.cpp**, **BranchFolding.hpp**

  Simplifies conditional branches with constant predicates or identical targets, reducing control‑flow complexity.

- **src/il/transform/SimplifyCFG/ForwardingElimination.cpp**, **ForwardingElimination.hpp**

  Removes redundant forwarding blocks that only pass parameters to a single successor, updating callers to jump directly.

- **src/il/transform/SimplifyCFG/ParamCanonicalization.cpp**, **ParamCanonicalization.hpp**

  Canonicalises block parameter lists and branch arguments to eliminate unused or reorderable parameters for cleaner CFGs.

- **src/il/transform/SimplifyCFG/PassContext.cpp**

  Shared context used by SimplifyCFG subpasses to share analysis and mutation helpers.

- **src/il/transform/SimplifyCFG/ReachabilityCleanup.cpp**, **ReachabilityCleanup.hpp**

  Prunes unreachable blocks after simplifications and updates traversal orders to keep the function well‑formed.

- **src/il/transform/SimplifyCFG/Utils.cpp**, **Utils.hpp**

- **src/il/transform/ConstFold.cpp**, **src/il/transform/ConstFold.hpp**, **src/il/transform/DCE.cpp**, **src/il/transform/DCE.hpp**

  Implement and declare constant folding and dead code elimination passes referenced by the optimizer pipeline.

- **src/il/transform/analysis/Liveness.cpp**, **src/il/transform/analysis/Liveness.hpp**, **src/il/transform/analysis/LoopInfo.cpp**

  Implementations for liveness and loop info analyses that complement the headers already mapped.

  Helper routines for inspecting predicates, block forms, and branch arguments used by SimplifyCFG transformations.

  Declares loop detection and summarization helpers built atop the CFG and dominator tree. Exposes loop nests and headers to enable LICM and simplify-cfg transformations.
- **src/il/transform/SimplifyCFG.cpp**, **src/il/transform/SimplifyCFG.hpp**

  Aggregated entry for the SimplifyCFG suite; wires subpasses together and exposes a single front door.

- SimplifyCFG headers (for quick lookup): **src/il/transform/SimplifyCFG/BlockMerging.hpp**, **src/il/transform/SimplifyCFG/BranchFolding.hpp**, **src/il/transform/SimplifyCFG/ForwardingElimination.hpp**, **src/il/transform/SimplifyCFG/ParamCanonicalization.hpp**, **src/il/transform/SimplifyCFG/ReachabilityCleanup.hpp**, **src/il/transform/SimplifyCFG/Utils.hpp**
