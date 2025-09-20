# CODEMAP

Paths to notable documentation and tests.

## Docs
- docs/overview.md — architecture overview
- docs/il-quickstart.md — beginner IL tutorial with runnable snippets
- docs/basic-reference.md — BASIC language reference and intrinsics
- docs/il-reference.md — full IL reference

## Front End (BASIC)
- **src/frontends/basic/ConstFolder.cpp**

  Implements compile-time folding for BASIC AST expressions using table-driven dispatch across arithmetic, comparison, and string operators. The folder walks expression subtrees, promotes numeric literals to the appropriate width, and replaces nodes with canonical `IntExpr`, `FloatExpr`, or `StringExpr` instances when evaluation succeeds. It preserves 64-bit wrap-around semantics and mutates AST nodes in place so later lowering phases see simplified trees. Dependencies include `ConstFolder.hpp`, `ConstFoldHelpers.hpp`, the expression class hierarchy, and standard utilities like `<optional>` for representing fold results.

- **src/frontends/basic/Lexer.cpp**

  Performs lexical analysis over BASIC source buffers with character-level tracking of offsets, line numbers, and column positions. The lexer normalizes whitespace, skips REM and apostrophe comments, and classifies keywords by uppercasing identifier sequences before token construction. Specialized scanners handle numeric literals with optional decimal, exponent, and type suffixes while preserving newline tokens for the parser's statement grouping logic. It depends on `Lexer.hpp`, token definitions declared through that header, `il::support::SourceLoc` for provenance, and the C++ standard library's `<cctype>` and `<string>` facilities.

- **src/frontends/basic/Lowerer.cpp**

  Coordinates the lowering pipeline that turns BASIC programs into IL modules, covering runtime helper declaration, name mangling, and SSA slot assignment. During `lowerProgram` it resets lowering state, builds an `il::build::IRBuilder`, and emits both user procedures and a synthetic `@main` while tracking string literals and array metadata. Helper structures like `BlockNamer` generate deterministic labels for branches, loops, and procedure prologues, and the implementation conditionally inserts bounds-check plumbing when the front end is configured for it. Dependencies span `Lowerer.hpp`, companion lowering helpers (`LowerEmit`, `LowerExpr`, `LowerRuntime`), the AST model, the name mangler, IL core headers, and standard containers and algorithms.

- **src/frontends/basic/NameMangler.cpp**

  Implements deterministic naming helpers used during BASIC lowering to produce SSA-friendly symbols. `nextTemp` increments an internal counter to yield sequential `%tN` temporaries while `block` ensures label hints are uniqued by appending numeric suffixes. Lowering routines call these helpers when emitting IL so control-flow edges and temporaries remain stable between runs. Dependencies include `frontends/basic/NameMangler.hpp` and the standard library containers declared there.

- **src/frontends/basic/NameMangler.hpp**

  Declares the `il::frontends::basic::NameMangler` utility that generates deterministic temporaries and block labels for BASIC programs. The class stores an incrementing counter and an `unordered_map` tracking how many times each block hint has been requested. Lowering code instantiates it per compilation so procedural lowering yields consistent names for diagnostics and pass comparisons. Dependencies include the C++ standard headers `<string>` and `<unordered_map>`, and consumers such as `Lowerer`, `LowerRuntime`, and `LoweringContext`.

- **src/frontends/basic/Parser.cpp**

  Coordinates the BASIC parser's top-level loop, priming the token stream, wiring statement handlers, and splitting procedures from main-line statements as it walks the source. It groups colon-separated statements into `StmtList` nodes and records whether the parser has entered the executable portion of the program so procedures stay at the top. Control flow and diagnostics are delegated to specialized handlers such as `parseIf`, `parseWhile`, and `parseFunction`, which are registered at construction time. The implementation depends on the lexer/token infrastructure, the AST node hierarchy (`Program`, `FunctionDecl`, `SubDecl`, `StmtList`), and `DiagnosticEmitter`/`il::support::SourceLoc` to surface parse errors.

- **src/frontends/basic/SemanticAnalyzer.cpp**

  Implements symbol, label, and type checking for BASIC programs by visiting the AST after parsing. It snapshots and restores scope state as it enters each procedure, tracks variable declarations, array usage, and procedure signatures, and enforces rules like “FUNCTION must return on every path.” Helper routines compute edit distances for better diagnostics, infer return guarantees, and propagate symbol bindings through nested scopes. The analyzer leans on `ProcRegistry`, `ScopeTracker`, AST statement/expr classes, the builtin registry, and the `DiagnosticEmitter` utilities to register procedures and emit targeted error codes.

## IL Analysis
- **src/il/analysis/CFG.cpp**

  Builds lightweight control-flow graph queries for IL functions without materializing persistent graph objects. The utilities collect successor and predecessor blocks by inspecting branch terminators, enabling passes to traverse edges by label resolution against the active module. They also compute post-order, reverse post-order, and topological orders while skipping unreachable blocks, providing canonical iteration sequences for analyses. Dependencies include `CFG.hpp`, IL core containers (`Module`, `Function`, `Block`, `Instr`, `Opcode`), the module registration shim in this file, and standard `<queue>`, `<stack>`, and unordered container types.

- **src/il/analysis/Dominators.cpp**

  Implements dominator tree construction atop the CFG helpers using the Cooper–Harvey–Kennedy algorithm. The builder walks reverse post-order sequences, intersects dominance paths for each block, and records immediate dominators along with child lists for tree traversal. Query helpers like `immediateDominator` and `dominates` then provide inexpensive dominance checks for optimization and verification passes. It relies on `Dominators.hpp`, the CFG API (`reversePostOrder`, `predecessors`), IL block objects, and the standard library's unordered maps.

## IL Build
- **src/il/build/IRBuilder.cpp**

  Offers a stateful API for constructing modules, functions, and basic blocks while keeping SSA bookkeeping consistent. It caches known callee return types, allocates temporaries, tracks insertion points, and synthesizes terminators like `br`, `cbr`, and `ret` with argument validation. Convenience helpers materialize constants, manage block parameters, and append instructions while enforcing single-terminator invariants per block. The builder relies on `il/build/IRBuilder.hpp`, IL core types (`Module`, `Function`, `BasicBlock`, `Instr`, `Type`, `Value`, `Opcode`), and `il::support::SourceLoc`, plus `<cassert>` and `<stdexcept>` for defensive checks.

## IL Core
- **src/il/core/Module.cpp**

  Provides the translation unit for IL modules, currently relying on inline definitions in the header for the aggregate container. Maintaining a dedicated source file keeps a stable location for future utilities such as explicit template instantiations or logging hooks. The empty namespace ensures build systems still emit an object file, which simplifies linking when libraries expect one. Dependencies include `il/core/Module.hpp`.

- **src/il/core/Module.hpp**

  Defines the lightweight `il::core::Module` aggregate that owns externs, globals, and function definitions for a compilation unit. The struct exposes a `version` string and vectors so front ends and parsers can build modules incrementally in deterministic order. Downstream passes and the VM inspect these containers to navigate the IR during analysis and execution. Dependencies include `il/core/Extern.hpp`, `il/core/Function.hpp`, `il/core/Global.hpp`, and the standard library `<string>`/`<vector>` containers.

- **src/il/core/Value.cpp**

  Provides constructors and formatting helpers for IL SSA values including temporaries, numeric literals, globals, and null pointers. The `toString` routine canonicalizes floating-point output by trimming trailing zeroes and ensuring deterministic formatting for the serializer, while helpers like `constInt` and `global` package values with the right tag. These utilities are widely used when building IR, pretty-printing modules, and interpreting values in the VM. The file depends on `il/core/Value.hpp` and the C++ standard library (`<sstream>`, `<iomanip>`, `<limits>`, `<utility>`) for string conversion.

## IL I/O
- **src/il/io/Serializer.cpp**

  Emits IL modules into textual form by traversing externs, globals, and function bodies with deterministic ordering when canonical mode is requested. It prints `.loc` metadata, rewrites operands using `Value::toString`, and honors opcode-specific formatting rules for calls, branches, loads/stores, and returns. Extern declarations can be sorted lexicographically to support diff-friendly output, and functions render their block parameters alongside instructions. Dependencies include the serializer interface, IL core containers (`Extern`, `Global`, `Function`, `BasicBlock`, `Instr`, `Module`, `Opcode`, `Value`), and the standard `<algorithm>`/`<sstream>` utilities.

## IL Runtime
- **src/il/runtime/RuntimeSignatures.cpp**

  Defines the shared registry mapping runtime helper names to IL signatures so frontends, verifiers, and the VM agree on the C ABI. A lazily initialized table enumerates every exported helper and wraps each return/parameter kind in `il::core::Type` objects, exposing lookup helpers for consumers. The data ensures extern declarations carry the right arity and type tags while giving the runtime bridge enough metadata to validate calls. Dependencies include `RuntimeSignatures.hpp`, IL core type definitions, and standard containers such as `<initializer_list>` and `<unordered_map>`.

## IL Transform
- **src/il/transform/Mem2Reg.cpp**

  Implements the sealed mem2reg algorithm that promotes stack slots introduced by `alloca` into SSA block parameters. The pass gathers allocation metadata, tracks reaching definitions per block, and patches branch arguments to thread promoted values through the CFG. It also maintains statistics about eliminated loads/stores and rewrites instructions in place so later passes see SSA form without detours through memory. Dependencies include `il/transform/Mem2Reg.hpp`, `il/analysis/CFG.hpp`, IL core types (`Function`, `BasicBlock`, `Instr`, `Value`, `Type`), and standard containers such as `<unordered_map>`, `<unordered_set>`, `<queue>`, `<optional>`, `<algorithm>`, and `<functional>`.

- **src/il/transform/Mem2Reg.hpp**

  Declares the public entry point for the mem2reg optimization along with an optional statistics structure. Clients provide an `il::core::Module` and receive the number of promoted variables and eliminated memory operations when they pass a `Mem2RegStats` pointer. The interface is used by the optimizer driver and test harnesses to promote locals before other analyses run. Dependencies include `il/core/Module.hpp`.

- **src/il/transform/PassManager.cpp**

  Hosts the modular pass manager that sequences module/function passes, wraps callbacks, and tracks analysis preservation across runs. It synthesizes CFG and liveness information to support passes, instantiates adapters that expose pass identifiers, and invalidates cached analyses when a pass does not declare them preserved. The implementation also provides helper factories for module/function pass lambdas and utilities to mark entire analysis sets as kept or dropped. Key dependencies span the pass manager headers, IL analysis utilities (`CFG`, `Dominators`, liveness builders), IL core containers, the verifier, and standard unordered containers.

## IL Verification
- **src/il/verify/Verifier.cpp**

  Validates whole modules by checking extern/global uniqueness, building block maps, and dispatching per-instruction structural and typing checks. It orchestrates control-flow validation, opcode contract enforcement, and type inference while collecting both hard errors and advisory diagnostics. Runtime signatures from the bridge are cross-checked against declared externs, and helper functions iterate each block to ensure terminators and branch arguments are well-formed. The verifier depends on `Verifier.hpp`, IL core data structures, the runtime signature catalog, analysis helpers (`TypeInference`, `ControlFlowChecker`, `InstructionChecker`), and the diagnostics framework (`support/diag_expected`).

## Support
- **src/support/arena.cpp**

  Defines the bump allocator primitives backing `il::support::Arena`. The constructor initializes an owned byte buffer and `allocate` performs aligned bumps while rejecting invalid power-of-two requests. Callers reset the arena between phases to reclaim memory for short-lived objects without individual frees. Dependencies include `support/arena.hpp` and the standard library facilities used by `std::vector` to manage the buffer.

- **src/support/arena.hpp**

  Declares the `il::support::Arena` class used to service fast, short-lived allocations for parsers and passes. It stores a `std::vector<std::byte>` buffer and a bump pointer so repeated `allocate` calls are O(1) until capacity runs out. The class exposes explicit reset semantics instead of per-allocation frees, making it a good fit for phase-based compilation. Dependencies include `<vector>`, `<cstddef>`, and modules that instantiate the arena such as parsers and VM helpers.

- **src/support/diagnostics.cpp**

  Implements the diagnostic engine that collects, counts, and prints errors and warnings emitted across the toolchain. It records severity information, formats messages with source locations when a `SourceManager` is provided, and exposes counters so clients can bail out after fatal issues. The printing helper maps enum severities to lowercase strings to keep output consistent between front-end and backend consumers. Dependencies cover the diagnostics interfaces, source management utilities, and standard stream facilities.

- **src/support/source_manager.cpp**

  Maintains canonical source-file identifiers and paths for diagnostics through the `SourceManager`. New files are normalized with `std::filesystem` so relative paths collapse to stable, platform-independent strings before being assigned incrementing IDs. Consumers such as the lexer, diagnostics engine, and tracing facilities call back into the manager to resolve `SourceLoc` instances into filenames. Dependencies include `source_manager.hpp` and the C++ `<filesystem>` library.

## VM Runtime
- **src/vm/Debug.cpp**

  Implements the VM's debugging controller responsible for breakpoints, watches, and source lookup. It normalizes file paths to compare breakpoints across host platforms, interns block labels for quick lookup, and tracks recently triggered locations to avoid duplicate stops. The controller also emits watch output when observed variables change, integrating with the interpreter loop's store callbacks. Dependencies include `vm/Debug.hpp`, IL core instruction/block definitions, the diagnostics source management helpers, and standard containers and streams from `<vector>`, `<string>`, `<unordered_map>`, and `<iostream>`.

- **src/vm/Debug.hpp**

  Exposes the `il::vm::DebugCtrl` interface that the VM uses to register and query breakpoints. The class manages collections of block-level and source-level breakpoints, normalizes user-supplied paths, and remembers the last triggered source location to support coalescing. It also maintains a map of watched variables so the interpreter can emit change notifications with type-aware formatting. Dependencies include `support/string_interner.hpp`, `support/symbol.hpp`, forward declarations of IL core `BasicBlock`/`Instr`, and standard headers `<optional>`, `<string_view>`, `<unordered_map>`, `<unordered_set>`, and `<vector>`.

- **src/vm/OpHandlers.cpp**

  Builds the opcode-dispatch table by translating metadata emitted from `il/core/Opcode.def` into concrete handler function pointers. Each opcode’s declared VM dispatch kind maps to a corresponding method on `OpHandlers`, allowing the VM to remain declarative and auto-updated when new opcodes are added. The table is materialized lazily and cached for reuse across VM instances to avoid recomputation. It depends on `vm/OpHandlers.hpp`, opcode metadata headers (`Opcode.hpp`, `OpcodeInfo.hpp`), and the generated definitions in `Opcode.def`.

- **src/vm/RuntimeBridge.cpp**

  Provides the dynamic bridge that allows the VM to invoke C runtime helpers while preserving precise trap diagnostics. On first use it materializes a dispatch table mapping runtime symbol names to thin adapters that unpack `Slot` arguments, call the underlying C functions, and marshal results back into VM slots. Before each call it records the current `SourceLoc`, function, and block names so the exported `vm_trap` hook can report accurate context if the callee aborts. The implementation relies on `RuntimeBridge.hpp`, the VM's `Slot` type, generated runtime headers such as `rt_math.h` and `rt_random.h`, and standard library utilities like `<unordered_map>` and `<sstream>`.

- **src/vm/Trace.cpp**

  Implements deterministic tracing facilities for the VM, emitting IL-level or source-level logs depending on the configured mode. Each step event walks the owning function's blocks to locate the instruction pointer, formats operands with locale-stable helpers, and optionally loads source snippets using the `SourceManager`. It supports Windows console quirks, writes to `stderr` with flush control, and ensures floating-point text matches serializer output. Dependencies include `Trace.hpp`, IL core instruction/value types, `support/source_manager.hpp`, `<locale>`, and `<filesystem>/<fstream>`.

- **src/vm/VM.cpp**

  Drives execution of IL modules by locating `main`, preparing frames, and stepping through instructions with debug and tracing hooks. The interpreter evaluates SSA values into runtime slots, routes opcodes through the handler table, and manages control-flow transitions within the execution loop. It coordinates with the runtime bridge for traps/globals and with debug controls to pause or resume execution as needed. Dependencies include `vm/VM.hpp`, IL core types (`Module`, `Function`, `BasicBlock`, `Instr`, `Value`, `Opcode`), the opcode handler table, tracing/debug infrastructure, and the `RuntimeBridge` C ABI.

- **src/vm/VMInit.cpp**

  Handles VM construction and per-function execution state initialization. The constructor captures module references, seeds lookup tables for functions and global strings, and wires tracing plus debugging facilities provided through `TraceConfig` and `DebugCtrl`. Supporting routines `setupFrame` and `prepareExecution` allocate register files, map block labels, and stage entry parameters so the main interpreter loop can run without additional setup. It depends on `VM.hpp`, IL core structures (`Module`, `Function`, `BasicBlock`, `Global`), runtime helpers like `rt_const_cstr`, and standard containers along with `<cassert>`.

