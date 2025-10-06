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

- **src/il/transform/PassManager.cpp**

  Hosts the modular pass manager that sequences module/function passes, wraps callbacks, and tracks analysis preservation across runs. It synthesizes CFG and liveness information to support passes, instantiates adapters that expose pass identifiers, and invalidates cached analyses when a pass does not declare them preserved. The implementation also provides helper factories for module/function pass lambdas and utilities to mark entire analysis sets as kept or dropped. Key dependencies span the pass manager headers, IL analysis utilities (`CFG`, `Dominators`, liveness builders), IL core containers, the verifier, and standard unordered containers.
