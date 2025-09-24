# CODEMAP

Paths to notable documentation and tests.

## Docs
- docs/architecture.md — architecture overview
- docs/il-guide.md#quickstart — beginner IL tutorial with runnable snippets
- docs/basic-language.md — BASIC language reference and intrinsics
- docs/il-guide.md#reference — full IL reference

## Front End (BASIC)
- **src/frontends/basic/AST.cpp**

  Defines the visitor `accept` overrides for every BASIC expression and statement node so passes can leverage double-dispatch instead of manual `switch` ladders. Each override simply forwards `*this` to the supplied `ExprVisitor` or `StmtVisitor`, preserving the tree's ownership model while letting the visitor decide behaviour. Keeping the logic centralized in one translation unit gives future extensions a stable home for additional helper code or instrumentation. Dependencies are limited to `frontends/basic/AST.hpp`, which declares the node types and visitor interfaces.

- **src/frontends/basic/AST.hpp**

  Declares the BASIC front-end abstract syntax tree covering all expression and statement variants emitted by the parser. Nodes record `SourceLoc` metadata, expose owned children via `std::unique_ptr`, and implement `accept` hooks so visitors can traverse without type introspection. Enumerations such as the builtin type tags and binary operator list capture language semantics for later passes. Dependencies include `support/source_location.hpp` alongside standard containers `<memory>`, `<string>`, and `<vector>`.

- **src/frontends/basic/AstPrinter.cpp**

  Renders BASIC ASTs into s-expression-style strings for debugging tools and golden tests. Nested `ExprPrinter` and `StmtPrinter` visitors walk every node type and stream formatted output through the shared `Printer` helper to keep formatting centralized. Formatting logic normalizes numeric output, resolves builtin names, and emits statement punctuation so the textual tree mirrors parser semantics. Dependencies include `frontends/basic/AstPrinter.hpp`, `frontends/basic/BuiltinRegistry.hpp`, and `<array>`/`<sstream>` for lookup tables and temporary buffers.

- **src/frontends/basic/BuiltinRegistry.cpp**

  Implements the BASIC builtin registry that maps canonical names to semantic and lowering callbacks shared across the front end. It materializes a dense table aligned with the `BuiltinCallExpr::Builtin` enum and a lookup map so passes can discover handlers without ad-hoc string switches. Lookup helpers return analyzer visitors, lowering functions, and scan hooks, letting semantic analysis enforce arity while lowering reuses shared emitters. Dependencies include `frontends/basic/BuiltinRegistry.hpp`, `frontends/basic/Lowerer.hpp`, `frontends/basic/SemanticAnalyzer.hpp`, and the standard `<array>` and `<unordered_map>` containers.

- **src/frontends/basic/BuiltinRegistry.hpp**

  Declares the data structures and lookup helpers that describe BASIC builtins to the rest of the front end. The `BuiltinInfo` struct captures each name, argument bounds, and member function pointers for semantic analysis, lowering, and pre-scan so callers dispatch without duplicating knowledge. Free functions expose both enum-indexed metadata and name-to-enum translation so parsing, analysis, and lowering operate off a single registry. Dependencies span the BASIC AST along with `frontends/basic/Lowerer.hpp`, `frontends/basic/SemanticAnalyzer.hpp`, and standard `<optional>`, `<string_view>`, `<vector>`, and `<cstddef>` headers.

- **src/frontends/basic/ConstFolder.cpp**

  Implements compile-time folding for BASIC AST expressions using table-driven dispatch across arithmetic, comparison, and string operators. The folder walks expression subtrees, promotes numeric literals to the appropriate width, and replaces nodes with canonical `IntExpr`, `FloatExpr`, or `StringExpr` instances when evaluation succeeds. It preserves 64-bit wrap-around semantics and mutates AST nodes in place so later lowering phases see simplified trees. Dependencies include `ConstFolder.hpp`, `ConstFoldHelpers.hpp`, the expression class hierarchy, and standard utilities like `<optional>` for representing fold results.

- **src/frontends/basic/Intrinsics.cpp**

  Defines the BASIC intrinsic registry as a compile-time table of names, return kinds, and parameter descriptors consumed by the parser and semantic analyzer. Helper arrays encode canonical signatures (string, numeric, and optional arguments) so the same data drives arity checking and lowering decisions. Public helpers `lookup` and `dumpNames` perform linear search and deterministic enumeration rather than building dynamic maps, keeping the footprint minimal. Dependencies include `frontends/basic/Intrinsics.hpp` for the descriptor types along with `<array>` and the stream facilities pulled in through that header.

- **src/frontends/basic/DiagnosticEmitter.cpp**

  Implements the BASIC diagnostic emitter that records error entries while forwarding them immediately to the shared `DiagnosticEngine`. It caches full source buffers keyed by file id so `printAll` can reproduce offending lines with caret markers and severity labels. Helper routines such as `emitExpected` centralize parser error wording while private helpers traverse stored text to extract a single line for display. The unit relies on `frontends/basic/DiagnosticEmitter.hpp` for the interface, which in turn brings in token metadata, `support::DiagnosticEngine`, `support::SourceManager`, and STL formatting helpers alongside `<algorithm>` and `<sstream>`.

- **src/frontends/basic/DiagnosticEmitter.hpp**

  Declares the BASIC `DiagnosticEmitter` class that wraps a `DiagnosticEngine` and `SourceManager` to report diagnostics with project-specific error codes. It defines the `Entry` record storing severity, code, message, location, and span so emissions can later be replayed to a stream. Public hooks let callers register raw source buffers, emit diagnostics with caret lengths, and print counts, while `emitExpected` standardizes parser mismatches. The header depends on the BASIC token definitions plus `support/diagnostics.hpp`, `support/source_manager.hpp`, and standard `<ostream>`, `<unordered_map>`, `<vector>`, and `<string>` facilities.

- **src/frontends/basic/Lexer.cpp**

  Performs lexical analysis over BASIC source buffers with character-level tracking of offsets, line numbers, and column positions. The lexer normalizes whitespace, skips REM and apostrophe comments, and classifies keywords by uppercasing identifier sequences before token construction. Specialized scanners handle numeric literals with optional decimal, exponent, and type suffixes while preserving newline tokens for the parser's statement grouping logic. It depends on `Lexer.hpp`, token definitions declared through that header, `il::support::SourceLoc` for provenance, and the C++ standard library's `<cctype>` and `<string>` facilities.

- **src/frontends/basic/LowerEmit.cpp**

  Hosts the lowering routines that translate BASIC programs and statements into IL instructions while preserving deterministic block layouts. The implementation walks parsed procedures, creates `main`, and seeds stack allocations before dispatching to statement-specific emitters such as `lowerIf` and `lowerFor`. It leans on helpers like `emitBoolFromBranches`, `emitAlloca`, and the `BlockNamer` embedded in `Lowerer` to guarantee stable SSA IDs and branch labels. Dependencies include `Lowerer.hpp`, `il::build::IRBuilder`, IL core block/function types, and runtime helper emitters declared in sibling headers.

- **src/frontends/basic/LowerEmit.hpp**

  Declares the private and public helpers `Lowerer` uses to emit IL for BASIC constructs ranging from builtins to control-flow statements. The header documents entry points for collecting variables, lowering expressions, and wiring loops or IF/ELSE chains while exposing a catalog of builtin handlers (`lowerLen`, `lowerMid`, and others). Its API ensures callers maintain consistent stack slot mappings and boolean lowering semantics by routing through shared helpers such as `lowerBoolBranchExpr`. Dependencies span the BASIC AST model, IL opcode/type definitions, and support utilities like `il::support::SourceLoc`, with implementations provided in `LowerEmit.cpp`.

- **src/frontends/basic/LowerExpr.cpp**

  Houses the expression-lowering portion of `Lowerer`, mapping BASIC AST nodes into IL SSA values. Helpers like `lowerVarExpr`, `lowerLogicalBinary`, and `lowerNumericBinary` reuse shared utilities for block management, type promotion, short-circuit control flow, and divide-by-zero trapping so emitted IL stays canonical. String operators route through runtime helpers, boolean expressions synthesize temporary blocks, and all visitors keep `curLoc` updated for diagnostics. Dependencies include `frontends/basic/Lowerer.hpp`, `frontends/basic/BuiltinRegistry.hpp`, IL core types (`BasicBlock`, `Function`, `Instr`), and `<functional>`/`<vector>` for callback plumbing.

- **src/frontends/basic/LowerStmt.cpp**

  Implements the statement lowering half of the BASIC front end by visiting AST nodes and emitting IL through the shared `Lowerer` state. A dedicated `LowererStmtVisitor` maps each AST class to the matching `lower*` helper, keeping `curLoc` and block pointers synchronized as assignments, control flow, and runtime calls are generated. The module allocates conditional and loop blocks deterministically, handles fallthrough by inserting explicit branches, and performs type coercions for assignments and prints before storing values. Dependencies include `frontends/basic/Lowerer.hpp`, IL core containers (`BasicBlock`, `Function`, `Instr`), and standard headers such as `<cassert>`, `<utility>`, and `<vector>`.

- **src/frontends/basic/Lowerer.cpp**

  Coordinates the lowering pipeline that turns BASIC programs into IL modules, covering runtime helper declaration, name mangling, and SSA slot assignment. During `lowerProgram` it resets lowering state, builds an `il::build::IRBuilder`, and emits both user procedures and a synthetic `@main` while tracking string literals and array metadata. Helper structures like `BlockNamer` generate deterministic labels for branches, loops, and procedure prologues, and the implementation conditionally inserts bounds-check plumbing when the front end is configured for it. Dependencies span `Lowerer.hpp`, companion lowering helpers (`LowerEmit`, `LowerExpr`, `LowerRuntime`), the AST model, the name mangler, IL core headers, and standard containers and algorithms.

- **src/frontends/basic/LoweringContext.cpp**

  Implements the state container that tracks BASIC variables, line blocks, and interned strings during lowering. Its helpers lazily create deterministic stack slot names, block objects, and literal identifiers so repeated queries stay stable across a compilation unit. The implementation works hand-in-hand with the `IRBuilder` to append blocks to the active `il::core::Function` as needed. Dependencies include `LoweringContext.hpp`, `il::build::IRBuilder`, and IL core block/function headers.

- **src/frontends/basic/LoweringContext.hpp**

  Defines the `LoweringContext` class responsible for memoizing per-procedure state such as slot names, block pointers, and string symbols. It exposes `getOrCreateSlot`, `getOrCreateBlock`, and `getOrAddString` so lowering helpers can allocate locals or jump targets without duplicating bookkeeping. The header captures references to the active `IRBuilder` and `Function`, and owns deterministic naming via `NameMangler` to align diagnostics with emitted IL. Dependencies include `NameMangler.hpp`, forward declarations for `il::build::IRBuilder` and IL core types, plus STL containers for the lookup tables.

- **src/frontends/basic/LowerRuntime.cpp**

  Implements the runtime-helper bookkeeping that lives inside `Lowerer`, ensuring each required BASIC runtime extern is declared once. `requestHelper` toggles bitset flags for string helpers, `trackRuntime` records math functions in deterministic order, and `declareRequiredRuntime` consults the shared signature registry before emitting externs through the IR builder. The routine also respects bounds-check configuration so only necessary traps and allocation helpers are imported. Dependencies include `frontends/basic/Lowerer.hpp`, `il/runtime/RuntimeSignatures.hpp`, and standard headers such as `<string>` and `<unordered_set>` used by the tracking containers.

- **src/frontends/basic/LowerRuntime.hpp**

  Extends `Lowerer` with nested definitions that track runtime helper usage, including the `RuntimeFn` enumeration, a hash functor, and containers that remember which math helpers were requested. It declares member hooks for requesting helpers, querying whether a helper is needed, recording runtime usage, and ultimately declaring the externs when lowering finishes a module. Because the header is included inside the class definition, these members can manipulate the surrounding lowering state without exposing implementation details elsewhere. Dependencies rely on the context provided by `Lowerer.hpp` for `RuntimeHelper` and `build::IRBuilder`, along with standard containers like `<vector>` and `<unordered_set>`.

- **src/frontends/basic/LowerScan.cpp**

  Performs the pre-pass that scans BASIC ASTs to infer expression types and runtime helper requirements prior to lowering. The routines walk expressions to propagate operand types, mark when string helpers such as `concat`, `mid`, or `val` are needed, and encode short-circuit rules for boolean operators. Scanning also inspects builtin metadata to delegate specialized handling while leaving numeric cases to default recursion. Dependencies include `frontends/basic/Lowerer.hpp`, `frontends/basic/BuiltinRegistry.hpp`, and `<string_view>` for suffix inference.

- **src/frontends/basic/NameMangler.cpp**

  Implements deterministic naming helpers used during BASIC lowering to produce SSA-friendly symbols. `nextTemp` increments an internal counter to yield sequential `%tN` temporaries while `block` ensures label hints are uniqued by appending numeric suffixes. Lowering routines call these helpers when emitting IL so control-flow edges and temporaries remain stable between runs. Dependencies include `frontends/basic/NameMangler.hpp` and the standard library containers declared there.

- **src/frontends/basic/NameMangler.hpp**

  Declares the `il::frontends::basic::NameMangler` utility that generates deterministic temporaries and block labels for BASIC programs. The class stores an incrementing counter and an `unordered_map` tracking how many times each block hint has been requested. Lowering code instantiates it per compilation so procedural lowering yields consistent names for diagnostics and pass comparisons. Dependencies include the C++ standard headers `<string>` and `<unordered_map>`, and consumers such as `Lowerer`, `LowerRuntime`, and `LoweringContext`.

- **src/frontends/basic/Parser.cpp**

  Coordinates the BASIC parser's top-level loop, priming the token stream, wiring statement handlers, and splitting procedures from main-line statements as it walks the source. It groups colon-separated statements into `StmtList` nodes and records whether the parser has entered the executable portion of the program so procedures stay at the top. Control flow and diagnostics are delegated to specialized handlers such as `parseIf`, `parseWhile`, and `parseFunction`, which are registered at construction time. The implementation depends on the lexer/token infrastructure, the AST node hierarchy (`Program`, `FunctionDecl`, `SubDecl`, `StmtList`), and `DiagnosticEmitter`/`il::support::SourceLoc` to surface parse errors.

- **src/frontends/basic/Parser_Token.cpp**

  Supplies the token-buffer management routines that back the BASIC parser's lookahead. `peek` lazily extends the buffered tokens from the lexer, `consume` pops the current token, and `expect` emits diagnostics before resynchronizing on mismatches. `syncToStmtBoundary` performs panic-mode recovery by scanning until end-of-line, colon, or end-of-file so parsing can resume cleanly. Dependencies include `frontends/basic/Parser.hpp`, `frontends/basic/DiagnosticEmitter.hpp`, and `<cstdio>` for fallback error messages.

- **src/frontends/basic/Parser_Expr.cpp**

  Hosts the Pratt-style expression parser that underpins BASIC expression parsing, starting from unary operators and recursing through precedence-filtered infix parsing. Specialized helpers handle numeric, string, and boolean literals, resolve builtin function calls via the registry, and recognize array or variable references with optional argument lists. The implementation centralizes builtin argument rules and ensures missing tokens still advance via `expect`, allowing diagnostic recovery without derailing the parse. Dependencies include `frontends/basic/Parser.hpp` for the parser state, `frontends/basic/BuiltinRegistry.hpp` for builtin lookups, and `<cstdlib>` along with AST node definitions transitively included there.

- **src/frontends/basic/Parser_Stmt.cpp**

  Implements statement-level parsing routines for BASIC, dispatching on a precomputed handler table to interpret keywords like PRINT, IF, FOR, and DIM. Helper functions manage colon-separated statement lists, colon/line-number loop bodies, and optional constructs such as STEP expressions, INPUT prompts, and ELSEIF cascades. It also builds function and subroutine declarations by collecting parameter metadata and recording END markers, maintaining parser state for array declarations and line numbers. Dependencies include `frontends/basic/Parser.hpp` (bringing in the AST, token kinds, and diagnostics plumbing) together with `<cstdlib>` for numeric conversions and the `il::support::SourceLoc` carried by the AST nodes.

- **src/frontends/basic/Parser.hpp**

  Declares the BASIC parser facade that coordinates token buffering and statement dispatch. It exposes `parseProgram` along with specialized helpers for each statement form, wiring a table of `StmtHandler` entries to member function pointers. Expression parsing utilities, loop body helpers, and DIM bookkeeping are declared here so front-end phases understand how control flow and arrays are surfaced before lowering. Dependencies include the BASIC AST model, lexer, diagnostic emitter, token helper headers, and standard containers such as `<array>`, `<vector>`, and `<unordered_set>`.

- **src/frontends/basic/ProcRegistry.cpp**

  Maintains the semantic registry of BASIC procedures and subs, rejecting duplicate declarations while materializing canonical signatures. `buildSignature` normalizes a descriptor into the `ProcSignature` table, enforcing unique parameter names and the array type limitations mandated by the language. The overloads of `registerProc` emit `B1004`/`B2004` diagnostics through the shared `SemanticDiagnostics` when clients attempt to redeclare procedures or use illegal parameter shapes, yet still record valid entries for later lookup. Dependencies include `frontends/basic/ProcRegistry.hpp` (pulling in the AST descriptors and diagnostics helpers) together with the standard `<unordered_set>` and `<utility>` headers.

- **src/frontends/basic/ScopeTracker.cpp**

  Implements the lexical scope stack the semantic analyzer uses to map BASIC identifiers onto unique SSA-friendly names. `pushScope`/`popScope` manage a vector of hash maps, while the nested `ScopedScope` type provides RAII guards for temporary scopes introduced by conditional and loop statements. `declareLocal` synthesizes deterministic suffixes and records them so `resolve` walks outward through the stack to find the nearest binding when identifiers are referenced later. Dependencies include `frontends/basic/ScopeTracker.hpp`, which defines the tracker and RAII helper, and the `<optional>`, `<string>`, `<unordered_map>`, and `<vector>` facilities used by the scope maps.

- **src/frontends/basic/SemanticAnalyzer.cpp**

  Implements symbol, label, and type checking for BASIC programs by visiting the AST after parsing. It snapshots and restores scope state as it enters each procedure, tracks variable declarations, array usage, and procedure signatures, and enforces rules like “FUNCTION must return on every path.” Helper routines compute edit distances for better diagnostics, infer return guarantees, and propagate symbol bindings through nested scopes. The analyzer leans on `ProcRegistry`, `ScopeTracker`, AST statement/expr classes, the builtin registry, and the `DiagnosticEmitter` utilities to register procedures and emit targeted error codes.

- **src/frontends/basic/SemanticAnalyzer.Exprs.cpp**

  Houses the expression visitor for the semantic analyzer, delegating each BASIC expression kind to helpers that resolve symbols, infer result types, and surface diagnostics. `SemanticAnalyzerExprVisitor` forwards to methods such as `analyzeVar`, `analyzeArray`, and `analyzeBinary`, allowing the analyzer to mutate AST nodes with resolved names while tracking type information. The implementation suggests spelling corrections, enforces numeric versus boolean operand contracts, and flags divide-by-zero cases or illegal builtin usage before lowering. Dependencies include `frontends/basic/SemanticAnalyzer.Internal.hpp` (which exposes the analyzer internals, AST, and diagnostic emitters) plus `<limits>` and `<sstream>` for suggestion heuristics.

- **src/frontends/basic/SemanticAnalyzer.Stmts.cpp**

  Implements the statement visitor driving BASIC semantic analysis, covering control-flow validation, scope management, and symbol tracking. `SemanticAnalyzerStmtVisitor` dispatches AST nodes to analyzer methods that resolve identifiers, maintain active FOR/NEXT stacks, and ensure each statement observes typing and flow rules. The pass checks IF/WHILE conditions, validates GOTO targets and DIM declarations, handles INPUT prompts, and coordinates array metadata with procedure scopes before lowering. Dependencies include `frontends/basic/SemanticAnalyzer.Internal.hpp`, which pulls in `ScopeTracker`, `ProcRegistry`, diagnostics utilities, and the broader AST model used during analysis.

- **src/frontends/basic/SemanticAnalyzer.Builtins.cpp**

  Implements builtin call analysis for the BASIC semantic analyzer. Dispatch helpers compute argument types, enforce arity bounds, and issue diagnostics when arguments are missing or of the wrong kind. Each builtin-specific handler records the inferred result type, toggles runtime helper requirements for string operations, and reuses shared messaging helpers for consistent errors. Dependencies come from `frontends/basic/SemanticAnalyzer.Internal.hpp`, `frontends/basic/BuiltinRegistry.hpp`, and `<sstream>` for constructing diagnostics.

- **src/frontends/basic/SemanticAnalyzer.Procs.cpp**

  Provides the procedure-scope machinery for BASIC semantic analysis, covering SUB/FUNCTION registration and body checking. `ProcedureScope` snapshots symbol tables, FOR stacks, labels, and array metadata so they can be restored when a declaration ends. The analyzer binds parameters, records labels, visits statements, and validates missing RETURN diagnostics before leaving each procedure. Dependencies include `frontends/basic/SemanticAnalyzer.Internal.hpp`, `<algorithm>`, and `<utility>` for bookkeeping.

- **src/frontends/basic/SemanticAnalyzer.hpp**

  Defines the semantic analyzer interface that walks the BASIC AST to record symbols, labels, and procedure signatures. It exposes diagnostic codes as constants and getters so later passes can inspect the collected scopes, label sets, and procedure registry. RAII helpers such as `ProcedureScope` snapshot symbol state across procedures while nested type-enum utilities describe inference results for expressions and builtin calls. Dependencies include `ProcRegistry`, `ScopeTracker`, `SemanticDiagnostics`, the BASIC AST classes, and standard containers used for symbol tables.

- **src/frontends/basic/SemanticDiagnostics.cpp**

  Provides the implementation of the semantic diagnostics façade that wraps the BASIC `DiagnosticEmitter`. Member functions forward severities, codes, and source ranges to the emitter while helper utilities format the standardized messaging for non-boolean conditions. The class also surfaces error and warning counters so the analyzer can decide when to abort compilation. Dependencies consist of `frontends/basic/SemanticDiagnostics.hpp` along with `<string>` and `<utility>` for assembling diagnostic text.

- **src/frontends/basic/Token.cpp**

  Implements the token kind to string mapper used by BASIC lexer and tooling diagnostics to render human-readable token names. The switch enumerates every `TokenKind`, covering keywords, operators, and sentinel values so debuggers and golden tests stay in sync with the parser. Because the function omits a default case it triggers compiler warnings whenever new token kinds are introduced, keeping the mapping complete over time. Dependencies are limited to `frontends/basic/Token.hpp` and the standard language support already included there.

## Codegen
- **src/codegen/x86_64/placeholder.cpp**

  Serves as the minimal translation unit anchoring the x86-64 code generation library so the component builds into a linkable object. It defines a stub `placeholder` function that returns zero, preserving the namespace until real emission stages arrive. The file includes nothing and therefore depends solely on the C++ core language, making it an isolated scaffold for future codegen work.

## IL Analysis
- **src/il/analysis/CFG.cpp**

  Builds lightweight control-flow graph queries for IL functions without materializing persistent graph objects. The utilities collect successor and predecessor blocks by inspecting branch terminators, enabling passes to traverse edges by label resolution against the active module. They also compute post-order, reverse post-order, and topological orders while skipping unreachable blocks, providing canonical iteration sequences for analyses. Dependencies include `CFG.hpp`, IL core containers (`Module`, `Function`, `Block`, `Instr`, `Opcode`), the module registration shim in this file, and standard `<queue>`, `<stack>`, and unordered container types.

- **src/il/analysis/CFG.hpp**

  Introduces lightweight control-flow graph queries that operate directly on IL modules without constructing persistent graph structures. Callers first set the active module and can then ask for successor, predecessor, post-order, or reverse-post-order traversals to drive analyses and transforms. The header also exposes acyclicity and topological ordering helpers so passes share consistent traversal contracts. Dependencies include IL core forward declarations for modules, functions, and blocks alongside the `<vector>` container.

- **src/il/analysis/Dominators.cpp**

  Implements dominator tree construction atop the CFG helpers using the Cooper–Harvey–Kennedy algorithm. The builder walks reverse post-order sequences, intersects dominance paths for each block, and records immediate dominators along with child lists for tree traversal. Query helpers like `immediateDominator` and `dominates` then provide inexpensive dominance checks for optimization and verification passes. It relies on `Dominators.hpp`, the CFG API (`reversePostOrder`, `predecessors`), IL block objects, and the standard library's unordered maps.

- **src/il/analysis/Dominators.hpp**

  Declares the `DomTree` structure that stores immediate dominator and child relationships for each block in an IL function. It provides convenience queries such as `dominates` and `immediateDominator` so optimization passes and verifiers can reason about control flow quickly. A standalone `computeDominatorTree` entry point promises a complete computation that the implementation backs with the Cooper–Harvey–Kennedy algorithm. Dependencies include IL core block/function types plus `<unordered_map>` and `<vector>` containers, and it pairs with `Dominators.cpp` which pulls in the CFG utilities.

## IL API
- **src/il/api/expected_api.cpp**

  Provides the v2 expected-based façade for the IL API, exposing thin wrappers that translate legacy boolean interfaces into `il::support::Expected` results. `parse_text_expected` forwards to the IL parser while preserving module ownership, and `verify_module_expected` defers to the verifier so callers can chain diagnostics without manual capture. The implementation intentionally contains no additional logic, ensuring the v1 and v2 entry points stay behaviorally identical apart from error propagation style. Dependencies include `il/api/expected_api.hpp`, `il/core/Module.hpp`, `il/io/Parser.hpp`, and `il/verify/Verifier.hpp`.

## IL Build
- **src/il/build/IRBuilder.cpp**

  Offers a stateful API for constructing modules, functions, and basic blocks while keeping SSA bookkeeping consistent. It caches known callee return types, allocates temporaries, tracks insertion points, and synthesizes terminators like `br`, `cbr`, and `ret` with argument validation. Convenience helpers materialize constants, manage block parameters, and append instructions while enforcing single-terminator invariants per block. The builder relies on `il/build/IRBuilder.hpp`, IL core types (`Module`, `Function`, `BasicBlock`, `Instr`, `Type`, `Value`, `Opcode`), and `il::support::SourceLoc`, plus `<cassert>` and `<stdexcept>` for defensive checks.

## IL Core
- **src/il/core/BasicBlock.cpp**

  Provides the dedicated translation unit for `BasicBlock`, keeping the struct’s definition linked even though all behaviour currently lives inline. Maintaining the `.cpp` file gives debuggers and future helper methods a stable location without forcing rebuilds of unrelated headers. Dependencies are limited to `il/core/BasicBlock.hpp`, which declares the block layout.

- **src/il/core/BasicBlock.hpp**

  Defines the `BasicBlock` aggregate that holds a label, parameter list, ordered instruction vector, and a terminator flag for every IL block. The struct documents invariants around unique labels, parameter arity, and terminator tracking so builders, verifiers, and the VM can reason about control flow consistently. Storing instructions and parameters by value keeps the IR layout contiguous for efficient traversal and serialization. Dependencies include `il/core/Instr.hpp`, `il/core/Param.hpp`, and standard `<string>`/`<vector>` containers.

- **src/il/core/Extern.cpp**

  Serves as the translation unit for IL extern declarations, keeping the `Extern` aggregate in its own object file even though no out-of-line logic is needed today. Maintaining the source file simplifies future expansions such as formatting helpers or explicit template instantiations without forcing widespread rebuilds. The unit depends solely on `il/core/Extern.hpp`.

- **src/il/core/Extern.hpp**

  Defines the `il::core::Extern` struct that models a module's imported functions. It stores the mangled name, declared return type, and ordered parameter list so verifiers and code generators can validate call sites against the runtime ABI. Documentation in the header captures invariants about unique names and signature alignment, making it clear how modules should populate the collection. Dependencies include `il/core/Type.hpp` together with standard `<string>` and `<vector>` containers.

- **src/il/core/Function.cpp**

  Serves as the translation unit for out-of-line helpers tied to `il::core::Function`, keeping the door open for richer logic as the IR grows. Even though functionality currently lives in the header, the dedicated source file guarantees a stable linkage point for debugging utilities or template specializations. Maintaining the file also keeps compile units consistent across build modes. Dependencies include `il/core/Function.hpp`.

- **src/il/core/Function.hpp**

  Models IL function definitions with their signature, parameter list, basic blocks, and SSA value names. Consumers mutate the `blocks` vector as they build or transform functions, while `params` and `retType` expose metadata to verifiers and backends. The struct's simple ownership semantics (value-stored blocks and params) make it easy for the builder, serializer, and VM to traverse the IR without extra indirection. Dependencies include `il/core/BasicBlock.hpp`, `il/core/Param.hpp`, `il/core/Type.hpp`, and standard `<string>`/`<vector>` containers.

- **src/il/core/Global.cpp**

  Provides the standalone compilation unit for IL globals, mirroring the pattern used for other core aggregates. Even though all behaviour currently lives inline, keeping a `.cpp` ensures debuggers and linkers always find a home for potential utility methods. The file only includes `il/core/Global.hpp`.

- **src/il/core/Global.hpp**

  Declares the `il::core::Global` record that describes module-scoped variables and constants. Fields capture the symbol name, IL type, and optional serialized initializer string, allowing the serializer, verifier, and VM to agree on storage layout. Comments document invariants around unique identifiers and initializer type matching so producers stay spec-compliant. It depends on `il/core/Type.hpp` and uses `<string>` for both identifiers and initializers.

- **src/il/core/Instr.cpp**

  Supplies the translation unit for `Instr`, preserving a place for future out-of-line helpers despite the representation being header-only today. The separate object file simplifies linking and debugging when instruction logic evolves. Dependencies consist solely of `il/core/Instr.hpp`.

- **src/il/core/Instr.hpp**

  Declares the `Instr` struct that models IL instructions with optional result ids, explicit type metadata, operand lists, callee names, successor labels, and branch arguments. Keeping results optional lets the same structure represent void terminators and value-producing instructions without extra subclasses. Operands and labels live in `std::vector`s so passes can append or rewrite them while maintaining deterministic ordering, and each instruction retains its `SourceLoc` for diagnostics. Dependencies include `il/core/Opcode.hpp`, `il/core/Type.hpp`, `il/core/Value.hpp`, `support/source_location.hpp`, and standard `<optional>`, `<string>`, and `<vector>` facilities.

- **src/il/core/Module.cpp**

  Provides the translation unit for IL modules, currently relying on inline definitions in the header for the aggregate container. Maintaining a dedicated source file keeps a stable location for future utilities such as explicit template instantiations or logging hooks. The empty namespace ensures build systems still emit an object file, which simplifies linking when libraries expect one. Dependencies include `il/core/Module.hpp`.

- **src/il/core/Module.hpp**

  Defines the lightweight `il::core::Module` aggregate that owns externs, globals, and function definitions for a compilation unit. The struct exposes a `version` string and vectors so front ends and parsers can build modules incrementally in deterministic order. Downstream passes and the VM inspect these containers to navigate the IR during analysis and execution. Dependencies include `il/core/Extern.hpp`, `il/core/Function.hpp`, `il/core/Global.hpp`, and the standard library `<string>`/`<vector>` containers.

- **src/il/core/OpcodeInfo.cpp**

  Materializes the opcode metadata table by expanding `Opcode.def` into `kOpcodeTable`, describing each opcode’s operand counts, result expectations, side effects, and VM dispatch category. Helper functions expose `getOpcodeInfo`, `isVariadicOperandCount`, and `toString` so verifiers, serializers, and diagnostics can query canonical opcode data. A static assertion ensures the table stays synchronized with the `Opcode` enumeration. Dependencies include `il/core/OpcodeInfo.hpp` and `<string>` for mnemonic conversion.

- **src/il/core/OpcodeInfo.hpp**

  Declares the metadata schema that annotates IL opcodes with result arity, operand type categories, successor counts, and interpreter dispatch hints. Enumerations such as `TypeCategory` and `VMDispatch` let passes reason about legality without parsing `Opcode.def`, while the `OpcodeInfo` struct captures the per-opcode contract in a compact form. The header also exposes the global `kOpcodeTable` and query helpers so callers can map from an `Opcode` to its metadata in constant time. Dependencies include `il/core/Opcode.hpp`, `<array>`, `<cstdint>`, and `<limits>`.

- **src/il/core/Type.cpp**

  Implements the lightweight `Type` wrapper’s constructor and the helpers that translate kind enumerators into canonical lowercase mnemonics. Centralizing these conversions keeps builders, serializers, and diagnostics aligned on the spec’s spelling. Dependencies include `il/core/Type.hpp`.

- **src/il/core/Type.hpp**

  Defines the `Type` value object that wraps primitive IL type kinds and exposes the `Kind` enumeration consumed throughout the compiler. The struct offers a simple constructor and `toString` interface so code can manipulate types without constructing heavyweight descriptors. Because the type is trivially copyable it can be stored by value in IR containers and diagnostic records. Dependencies include `<string>` for string conversions.

- **src/il/core/Value.cpp**

  Provides constructors and formatting helpers for IL SSA values including temporaries, numeric literals, globals, and null pointers. The `toString` routine canonicalizes floating-point output by trimming trailing zeroes and ensuring deterministic formatting for the serializer, while helpers like `constInt` and `global` package values with the right tag. These utilities are widely used when building IR, pretty-printing modules, and interpreting values in the VM. The file depends on `il/core/Value.hpp` and the C++ standard library (`<sstream>`, `<iomanip>`, `<limits>`, `<utility>`) for string conversion.

## IL I/O
- **src/il/io/FunctionParser.cpp**

  Implements the low-level text parser for IL function bodies, decoding headers, block labels, and instruction streams into the mutable `ParserState`. Header parsing splits out parameter lists, return types, and seeds temporary IDs, while block parsing validates parameter arity and reconciles pending branch argument counts. The main `parseFunction` loop walks the textual body line by line, honoring `.loc` directives, delegating instruction syntax to `parseInstruction`, and emitting structured diagnostics via the capture helpers. Dependencies include `il/io/FunctionParser.hpp`, `il/core` definitions for modules, functions, blocks, and params, the sibling `InstrParser`, `ParserUtil`, `TypeParser`, and diagnostic support from `support/diag_expected.hpp`.

- **src/il/io/InstrParser.cpp**

  Parses individual IL instruction lines into `il::core::Instr` instances while updating the parser state that tracks temporaries, blocks, and diagnostics. Utility routines decode operands into values or types, apply opcode metadata to validate operand counts, result arity, and successor lists, and enqueue branch targets for later block resolution. Specialized parsers handle calls, branch target lists, and SSA assignment prefixes so the textual form produced by the serializer round-trips cleanly. Dependencies include `il/io/InstrParser.hpp`, IL core opcode/type/value headers, helper utilities from `ParserUtil` and `TypeParser`, and diagnostic plumbing in `support/diag_expected.hpp`.

- **src/il/io/ModuleParser.cpp**

  Implements the module-level IL parser responsible for directives like `il`, `extern`, `global`, and `func`. Helpers normalize tokens, parse type lists via `parseType`, capture diagnostics from the function parser, and repackage failures as `Expected` errors tied to the current line. Extern and global directives mutate the active `ParserState` in place, while function headers dispatch into the dedicated function parser and version directives update module metadata. Dependencies include `il/io/ModuleParser.hpp`, IL core containers, subordinate parsers (`FunctionParser`, `ParserUtil`, `TypeParser`), `support/diag_expected.hpp`, and standard `<sstream>`, `<string_view>`, `<utility>`, and `<vector>` utilities.

- **src/il/io/ModuleParser.hpp**

  Declares the `parseModuleHeader` helper that advances the IL reader through top-level directives. Callers provide an input stream, the current line buffer, and the shared `ParserState` so externs, globals, and functions are appended directly to the module. Errors are streamed to an `std::ostream`, mirroring the rest of the parsing layer while keeping this interface minimal. Dependencies are limited to `il/io/ParserState.hpp` alongside `<istream>`, `<ostream>`, and `<string>`.

- **src/il/io/Parser.cpp**

  Implements the façade for parsing textual IL modules from an input stream. It seeds a `ParserState`, normalizes each line while skipping comments or blanks, and then hands structural decisions to the detail `parseModuleHeader_E` helper. Errors from that helper propagate unchanged so callers receive consistent diagnostics with precise line numbers. Dependencies include `Parser.hpp`, `ModuleParser.hpp`, `ParserUtil.hpp`, the parser-state helpers, IL core `Module` definitions, and the diagnostics `Expected` wrapper.

- **src/il/io/ParserUtil.cpp**

  Collects small lexical helpers shared by the IL text parser, including trimming whitespace, reading comma-delimited tokens, and parsing integer or floating literal spellings. Each function wraps the corresponding standard-library conversion while enforcing full-token consumption so upstream parsers can surface precise errors. They are intentionally stateless and operate on caller-provided buffers to keep instruction parsing allocation-free. Dependencies include `il/io/ParserUtil.hpp` together with `<cctype>` and `<exception>` from the standard library.

- **src/il/io/Parser.hpp**

  Declares the IL parser entry point that orchestrates module, function, and instruction sub-parsers. The class exposes a single static `parse` routine, signaling that parsing is a stateless operation layered over a supplied module instance. Its includes reveal the composition of specialized parsers and parser-state bookkeeping while documenting the diagnostic channel used for reporting errors. Dependencies include IL core forward declarations, the function/instruction/module parser headers, `ParserState.hpp`, and the `il::support::Expected` utility.

- **src/il/io/ParserState.cpp**

  Implements the lightweight constructor for the shared IL parser state, wiring the mutable context to the module being populated. Having the definition in a `.cpp` avoids inlining across translation units that include the header. The only dependency is `il/io/ParserState.hpp`.

- **src/il/io/ParserState.hpp**

  Declares `il::io::detail::ParserState`, the mutable context threaded through module, function, and instruction parsers. It keeps references to the current module, function, and basic block along with SSA bookkeeping structures like `tempIds`, `nextTemp`, and unresolved branch metadata. The nested `PendingBr` struct and `blockParamCount` map let parsers defer validation until all labels are seen while `curLoc` tracks active `.loc` directives for diagnostics. Dependencies cover `il/core/fwd.hpp`, `support/source_location.hpp`, and standard `<string>`, `<unordered_map>`, and `<vector>` utilities.

- **src/il/io/TypeParser.cpp**

  Translates textual IL type mnemonics like `i64`, `ptr`, or `str` into `il::core::Type` objects used by the parser, returning a default type when the spelling is unknown. Callers can optionally receive a success flag via the `ok` pointer, allowing higher-level parsers to differentiate between absent and malformed type annotations. The mapping mirrors the primitive set documented in `docs/il-guide.md#reference`, ensuring serializer and parser stay aligned on accepted spellings. Dependencies include `il/io/TypeParser.hpp`, which exposes the interface backed by `il::core::Type` definitions.

- **src/il/io/Serializer.cpp**

  Emits IL modules into textual form by traversing externs, globals, and function bodies with deterministic ordering when canonical mode is requested. It prints `.loc` metadata, rewrites operands using `Value::toString`, and honors opcode-specific formatting rules for calls, branches, loads/stores, and returns. Extern declarations can be sorted lexicographically to support diff-friendly output, and functions render their block parameters alongside instructions. Dependencies include the serializer interface, IL core containers (`Extern`, `Global`, `Function`, `BasicBlock`, `Instr`, `Module`, `Opcode`, `Value`), and the standard `<algorithm>`/`<sstream>` utilities.

## IL Runtime
- **src/il/runtime/RuntimeSignatures.cpp**

  Defines the shared registry mapping runtime helper names to IL signatures so frontends, verifiers, and the VM agree on the C ABI. A lazily initialized table enumerates every exported helper and wraps each return/parameter kind in `il::core::Type` objects, exposing lookup helpers for consumers. The data ensures extern declarations carry the right arity and type tags while giving the runtime bridge enough metadata to validate calls. Dependencies include `RuntimeSignatures.hpp`, IL core type definitions, and standard containers such as `<initializer_list>` and `<unordered_map>`.

- **src/il/runtime/RuntimeSignatures.hpp**

  Describes the metadata schema for runtime helper signatures shared across the toolchain. It defines the `RuntimeSignature` struct capturing return and parameter types using IL type objects and documents how parameter order mirrors the C ABI. Accessor functions expose the registry map and an optional lookup helper so consumers can fetch signatures lazily without copying data. Dependencies include `il/core/Type.hpp`, `<string_view>`, `<vector>`, and `<unordered_map>`.

## IL Transform
- **src/il/transform/ConstFold.cpp**

  Runs the IL constant-folding pass, replacing integer arithmetic and recognised runtime math intrinsics with precomputed values. Helpers such as `wrapAdd`/`wrapMul` model modulo 2^64 behaviour so folded results mirror VM semantics, and `foldCall` maps literal arguments onto runtime helpers like `rt_abs`, `rt_floor`, and `rt_pow`. The pass walks every function, substitutes the folded value via `replaceAll`, and erases the defining instruction in place to keep blocks minimal while respecting domain checks. Dependencies include `il/transform/ConstFold.hpp`, IL core containers (`Module`, `Function`, `Instr`, `Value`), and the standard `<cmath>`, `<cstdint>`, `<cstdlib>`, and `<limits>` headers.

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

## IL Utilities
- **src/il/utils/Utils.cpp**

  Collects small IL convenience helpers used across analyses to query blocks and instructions without materializing extra structures. `belongsToBlock` performs linear membership tests over a block's instruction vector, while `terminator` and `isTerminator` centralize opcode-based control-flow classification. These utilities back verifier and optimizer code that need quick checks when rewriting IR without duplicating opcode tables. Dependencies include `il/utils/Utils.hpp` together with `il/core/BasicBlock.hpp`, `il/core/Instr.hpp`, and `il/core/Opcode.hpp` for the IR data structures.

## IL Verification
- **src/il/verify/ControlFlowChecker.cpp**

  Handles verifier checks specific to IL control flow, ensuring blocks and terminators obey structural rules. Helpers like `validateBlockParams_E` seed type information for block parameters, `iterateBlockInstructions_E` walks instructions until the terminator, and `checkBlockTerminators_E` enforces the single-terminator rule from the IL spec. Branch utilities validate jump targets, branch argument counts, and type compatibility while cross-referencing maps of reachable blocks, externs, and functions. Dependencies include `il/verify/ControlFlowChecker.hpp`, `il/verify/TypeInference.hpp`, IL core containers (`BasicBlock`, `Instr`, `Function`, `Extern`, `Param`), `support/diag_expected.hpp`, and standard `<functional>`, `<sstream>`, `<string_view>`, and `<unordered_set>` facilities.

- **src/il/verify/InstructionChecker.cpp**

  Provides verifier checks for non-control-flow IL instructions by pairing opcode metadata with type inference. `verifyOpcodeSignature_E` enforces operand/result counts and successor arity from `OpcodeInfo`, while targeted helpers validate allocas, memory operations, arithmetic, and runtime calls. Diagnostics format instruction snippets, queue optional warnings, and power both streaming and `Expected`-based verifier entry points so tooling can choose its preferred API. Dependencies include `il/verify/InstructionChecker.hpp`, `il/verify/TypeInference.hpp`, IL core types (`Function`, `BasicBlock`, `Instr`, `Extern`, `OpcodeInfo`), and `support/diag_expected.hpp` together with `<sstream>`, `<string_view>`, `<unordered_map>`, and `<vector>` from the standard library.

- **src/il/verify/TypeInference.cpp**

  Provides the verifier's type-inference engine for IL, backing operand validation and diagnostic rendering. Construction ties the helper to caller-owned maps and sets so it can track temporary types, mark definitions, and uphold the invariant that every defined id has an associated type. Utility methods render single-line instruction snippets, compute primitive widths, ensure operands are defined, and surface failures as either streamed diagnostics or `Expected` errors. The implementation includes `il/verify/TypeInference.hpp`, draws on IL core instruction and value metadata, and uses `support/diag_expected.hpp` plus `<sstream>` and `<string_view>` to produce rich error text.

- **src/il/verify/TypeInference.hpp**

  Defines the `TypeInference` helper interface that the verifier uses to reason about IL operand types. It offers queries for value types and byte widths along with mutation hooks to record or drop temporaries as control flow progresses. Both streaming and `Expected`-returning verification APIs share the same backing state, giving callers flexibility without duplicating logic. Dependencies span IL core headers for types, values, and forward declarations together with `<unordered_map>`, `<unordered_set>`, `<ostream>`, and `<string>` from the standard library.

- **src/il/verify/Verifier.cpp**

  Validates whole modules by checking extern/global uniqueness, building block maps, and dispatching per-instruction structural and typing checks. It orchestrates control-flow validation, opcode contract enforcement, and type inference while collecting both hard errors and advisory diagnostics. Runtime signatures from the bridge are cross-checked against declared externs, and helper functions iterate each block to ensure terminators and branch arguments are well-formed. The verifier depends on `Verifier.hpp`, IL core data structures, the runtime signature catalog, analysis helpers (`TypeInference`, `ControlFlowChecker`, `InstructionChecker`), and the diagnostics framework (`support/diag_expected`).

## Support
- **src/support/arena.cpp**

  Defines the bump allocator primitives backing `il::support::Arena`. The constructor initializes an owned byte buffer and `allocate` performs aligned bumps while rejecting invalid power-of-two requests. Callers reset the arena between phases to reclaim memory for short-lived objects without individual frees. Dependencies include `support/arena.hpp` and the standard library facilities used by `std::vector` to manage the buffer.

- **src/support/arena.hpp**

  Declares the `il::support::Arena` class used to service fast, short-lived allocations for parsers and passes. It stores a `std::vector<std::byte>` buffer and a bump pointer so repeated `allocate` calls are O(1) until capacity runs out. The class exposes explicit reset semantics instead of per-allocation frees, making it a good fit for phase-based compilation. Dependencies include `<vector>`, `<cstddef>`, and modules that instantiate the arena such as parsers and VM helpers.

- **src/support/diag_expected.cpp**

  Supplies the plumbing for `Expected<void>` diagnostics used across the toolchain to report recoverable errors. It implements the boolean conversion and error accessor, provides consistent severity-to-string mapping, and centralizes error construction through `makeError`. `printDiag` consults the shared `SourceManager` to prepend file and line information so messages match compiler-style output. Dependencies include `support/diag_expected.hpp`, which defines the diagnostic types and pulls in `<ostream>`, `<string>`, and the `SourceLoc` helpers.

- **src/support/diag_capture.cpp**

  Supplies the out-of-line utilities for `DiagCapture`, converting captured diagnostic buffers into stream output or `Expected<void>` results. The helper forwards to `printDiag` for rendering, and `toDiag` reuses `makeError` so stored text becomes a structured diagnostic tied to an empty source location. `capture_to_expected_impl` bridges legacy boolean-returning APIs into the newer Expected-based flow by either returning success or the captured error. Dependencies include `support/diag_capture.hpp`, which brings in the diagnostic primitives and Expected helpers these adapters rely on.

- **src/support/diagnostics.cpp**

  Implements the diagnostic engine that collects, counts, and prints errors and warnings emitted across the toolchain. It records severity information, formats messages with source locations when a `SourceManager` is provided, and exposes counters so clients can bail out after fatal issues. The printing helper maps enum severities to lowercase strings to keep output consistent between front-end and backend consumers. Dependencies cover the diagnostics interfaces, source management utilities, and standard stream facilities.

- **src/support/diagnostics.hpp**

  Advertises the diagnostics subsystem responsible for collecting, counting, and printing compiler messages. The header enumerates severity levels, the `Diagnostic` record, and the `DiagnosticEngine` API so callers can report events and later flush them to a stream. Counter accessors make it easy for front ends to guard execution on accumulated errors while preserving the order of recorded messages. Dependencies include the shared `source_location.hpp` definitions and standard library facilities such as `<ostream>`, `<string>`, and `<vector>`.

- **src/support/source_manager.cpp**

  Maintains canonical source-file identifiers and paths for diagnostics through the `SourceManager`. New files are normalized with `std::filesystem` so relative paths collapse to stable, platform-independent strings before being assigned incrementing IDs. Consumers such as the lexer, diagnostics engine, and tracing facilities call back into the manager to resolve `SourceLoc` instances into filenames. Dependencies include `source_manager.hpp` and the C++ `<filesystem>` library.

- **src/support/string_interner.cpp**

  Provides the implementation of the `StringInterner`, giving the BASIC front end and VM a shared symbol table. `intern` consults an unordered map before copying new strings into the storage vector, guaranteeing each interned value receives a stable non-zero `Symbol`. The `lookup` helper validates ids and returns the original view or an empty result when the caller passes the reserved sentinel. It relies on `support/string_interner.hpp`, which supplies the container members and the `Symbol` wrapper.

- **src/support/string_interner.hpp**

  Declares the `StringInterner` class and accompanying `Symbol` abstraction used wherever the toolchain needs canonicalized identifiers. The interface exposes `intern` to deduplicate strings and `lookup` to retrieve the original text, making it easy for diagnostics, debuggers, and registries to share keys. Internally the class stores an unordered map from text to `Symbol` alongside a vector of owned strings so views remain valid for the interner's lifetime. Dependencies include `support/symbol.hpp` plus standard `<string>`, `<string_view>`, `<unordered_map>`, and `<vector>` containers.

- **src/support/symbol.cpp**

  Defines comparison and utility operators for the interned `Symbol` identifier type. Equality and inequality simply compare the stored integral id, while the boolean conversion treats zero as the reserved invalid sentinel. A `std::hash` specialization reuses the id so symbols integrate directly with unordered containers. Dependencies are limited to `support/symbol.hpp`, which declares the wrapper and exposes the underlying field.

- **src/support/source_location.cpp**

  Implements the helper that reports whether a `SourceLoc` points at a registered file. The method checks for a nonzero file identifier so diagnostics and tools can ignore default-constructed locations. Dependencies include only `support/source_location.hpp`, which defines the lightweight value type.

## VM Runtime
- **src/vm/control_flow.cpp**

  Implements interpreter handlers for branch, call, return, and trap opcodes, centralizing control-flow manipulation inside `OpHandlers`. A shared helper moves execution to successor blocks, seeds block parameters, and flags jumps in the `ExecResult`, while other handlers evaluate call operands and route extern invocations through the `RuntimeBridge`. The logic keeps frame state consistent so the main VM loop can honour returns and traps without manual bookkeeping. Dependencies include `vm/OpHandlers.hpp`, `vm/OpHandlerUtils.hpp`, `vm/RuntimeBridge.hpp`, and IL core definitions for `BasicBlock`, `Function`, `Instr`, and `Value`.

- **src/vm/Debug.cpp**

  Implements the VM's debugging controller responsible for breakpoints, watches, and source lookup. It normalizes file paths to compare breakpoints across host platforms, interns block labels for quick lookup, and tracks recently triggered locations to avoid duplicate stops. The controller also emits watch output when observed variables change, integrating with the interpreter loop's store callbacks. Dependencies include `vm/Debug.hpp`, IL core instruction/block definitions, the diagnostics source management helpers, and standard containers and streams from `<vector>`, `<string>`, `<unordered_map>`, and `<iostream>`.

- **src/vm/Debug.hpp**

  Exposes the `il::vm::DebugCtrl` interface that the VM uses to register and query breakpoints. The class manages collections of block-level and source-level breakpoints, normalizes user-supplied paths, and remembers the last triggered source location to support coalescing. It also maintains a map of watched variables so the interpreter can emit change notifications with type-aware formatting. Dependencies include `support/string_interner.hpp`, `support/symbol.hpp`, forward declarations of IL core `BasicBlock`/`Instr`, and standard headers `<optional>`, `<string_view>`, `<unordered_map>`, `<unordered_set>`, and `<vector>`.

- **src/vm/DebugScript.cpp**

  Parses debugger automation scripts so the VM can replay predetermined actions during execution. The constructor reads a text file, mapping commands like `continue`, `step`, and `step N` into queued `DebugAction` records while logging unexpected lines to `stderr` as `[DEBUG]` diagnostics. Helper methods let callers append step requests programmatically and retrieve the next action, defaulting to Continue when the queue empties. Dependencies include `vm/DebugScript.hpp` along with standard `<fstream>`, `<iostream>`, and `<sstream>` utilities for file handling.

- **src/vm/DebugScript.hpp**

  Declares the `DebugScript` FIFO wrapper used by the VM debugger to provide scripted actions. It defines the `DebugActionKind` enum, the simple `DebugAction` POD, and member functions for loading scripts, enqueuing steps, polling actions, and checking emptiness. The class stores actions in a `std::queue`, making consumption order explicit for the interpreter's debug loop. Dependencies cover standard headers `<cstdint>`, `<queue>`, `<string>`, with behavior implemented in `DebugScript.cpp`.

- **src/vm/fp_ops.cpp**

  Contains the VM’s floating-point arithmetic and comparison handlers, factoring operand evaluation into templated helpers that write back through `ops::storeResult`. Each opcode relies on host IEEE-754 semantics for doubles, ensuring NaNs and infinities propagate while comparisons return canonical 0/1 truth values. The helpers isolate frame mutation so arithmetic handlers remain side-effect free aside from updating the destination slot. Dependencies include `vm/OpHandlers.hpp`, `vm/OpHandlerUtils.hpp`, and IL instruction metadata.

- **src/vm/int_ops.cpp**

  Provides the integer arithmetic, bitwise, shift, and comparison handlers executed by the VM. Shared helpers evaluate operands once and encode the arithmetic or predicate rule, guaranteeing two’s complement wrap-around semantics align with the IL reference. Comparison results are normalized to IL booleans and stored via `ops::storeResult`, preserving invariants expected by later opcodes. Dependencies include `vm/OpHandlers.hpp`, `vm/OpHandlerUtils.hpp`, and IL instruction definitions.

- **src/vm/mem_ops.cpp**

  Implements the VM opcode handlers for memory-centric IL instructions such as `alloca`, `load`, `store`, `gep`, and constant-string helpers. Each routine evaluates operands through the VM, enforces stack bounds or null checks, and mirrors type-directed load/store semantics so runtime behaviour matches the IL specification. Store handlers also funnel writes through the debug controller, and pointer math reuses shared slot helpers to keep execution deterministic. Dependencies cover `vm/OpHandlers.hpp`, `vm/OpHandlerUtils.hpp`, `vm/RuntimeBridge.hpp`, IL core instruction/type definitions, and standard `<cassert>` and `<cstring>` facilities.

- **src/vm/OpHandlerUtils.cpp**

  Provides shared helper routines used by opcode implementations to manipulate VM state. The `storeResult` utility grows the register vector as needed before writing an instruction's destination slot, ensuring the interpreter never reads uninitialized registers. Keeping the logic centralized prevents handlers from duplicating resize and assignment code and maintains invariant checks in one location. Dependencies include `vm/OpHandlerUtils.hpp` and IL instruction definitions from `il/core/Instr.hpp`.

- **src/vm/OpHandlerUtils.hpp**

  Declares reusable helpers available to opcode handlers, currently exposing `storeResult` within the `il::vm::detail::ops` namespace. The header wires in `vm/VM.hpp` so helpers can operate on frames and slot unions without additional includes. Housing the declarations separately lets opcode sources include lightweight utilities without dragging in the entire handler table. Dependencies include `vm/VM.hpp` and IL instruction forward declarations, with implementations in `OpHandlerUtils.cpp`.

- **src/vm/OpHandlers.cpp**

  Builds the opcode-dispatch table by translating metadata emitted from `il/core/Opcode.def` into concrete handler function pointers. Each opcode’s declared VM dispatch kind maps to a corresponding method on `OpHandlers`, allowing the VM to remain declarative and auto-updated when new opcodes are added. The table is materialized lazily and cached for reuse across VM instances to avoid recomputation. It depends on `vm/OpHandlers.hpp`, opcode metadata headers (`Opcode.hpp`, `OpcodeInfo.hpp`), and the generated definitions in `Opcode.def`.

- **src/vm/OpHandlers.hpp**

  Advertises the `il::vm::detail::OpHandlers` struct whose static methods implement each IL opcode the interpreter supports. Every handler receives the active `VM`, frame, decoded instruction, and block map so it can evaluate operands, mutate registers, branch, or trigger runtime calls, with shared plumbing factored into `OpHandlerUtils`. The header also exposes `getOpcodeHandlers`, the accessor that returns the lazily built dispatch table consumed by the main interpreter loop. It depends on `vm/VM.hpp` for `Frame`, `ExecResult`, and block metadata, which in turn pull in IL instruction types and the runtime bridge contracts.

- **src/vm/RuntimeBridge.cpp**

  Provides the dynamic bridge that allows the VM to invoke C runtime helpers while preserving precise trap diagnostics. On first use it materializes a dispatch table mapping runtime symbol names to thin adapters that unpack `Slot` arguments, call the underlying C functions, and marshal results back into VM slots. Before each call it records the current `SourceLoc`, function, and block names so the exported `vm_trap` hook can report accurate context if the callee aborts. The implementation relies on `RuntimeBridge.hpp`, the VM's `Slot` type, generated runtime headers such as `rt_math.h` and `rt_random.h`, and standard library utilities like `<unordered_map>` and `<sstream>`.

- **src/vm/RuntimeBridge.hpp**

  Declares the static `RuntimeBridge` adapters that let the VM invoke the C runtime and surface traps. The `call` helper marshals evaluated slot arguments into the runtime ABI while threading source locations, function names, and block labels for diagnostics. A companion `trap` entry centralizes error reporting so runtime failures share consistent messaging paths. Dependencies include `rt.hpp`, `support/source_location.hpp`, the forward-declared VM `Slot` union, and standard `<string>` and `<vector>` containers.

- **src/vm/Trace.cpp**

  Implements deterministic tracing facilities for the VM, emitting IL-level or source-level logs depending on the configured mode. Each step event walks the owning function's blocks to locate the instruction pointer, formats operands with locale-stable helpers, and optionally loads source snippets using the `SourceManager`. It supports Windows console quirks, writes to `stderr` with flush control, and ensures floating-point text matches serializer output. Dependencies include `Trace.hpp`, IL core instruction/value types, `support/source_manager.hpp`, `<locale>`, and `<filesystem>/<fstream>`.

- **src/vm/Trace.hpp**

  Declares the tracing configuration and sink used by the VM to emit execution logs. `TraceConfig` models the available modes and can carry a `SourceManager` pointer to support source-aware traces. `TraceSink` exposes an `onStep` callback that the interpreter invokes with each instruction and active frame so the implementation can render deterministic text. Dependencies include IL instruction forward declarations, `il::support::SourceManager`, the VM `Frame` type, and the standard headers consumed by `Trace.cpp`.

- **src/vm/VM.cpp**

  Drives execution of IL modules by locating `main`, preparing frames, and stepping through instructions with debug and tracing hooks. The interpreter evaluates SSA values into runtime slots, routes opcodes through the handler table, and manages control-flow transitions within the execution loop. It coordinates with the runtime bridge for traps/globals and with debug controls to pause or resume execution as needed. Dependencies include `vm/VM.hpp`, IL core types (`Module`, `Function`, `BasicBlock`, `Instr`, `Value`, `Opcode`), the opcode handler table, tracing/debug infrastructure, and the `RuntimeBridge` C ABI.

- **src/vm/VM.hpp**

  Defines the VM's public interface, including the slot union, execution frame container, and the interpreter class itself. The header documents how `VM` wires together tracing, debugging, and opcode dispatch, exposing the `ExecResult` structure and handler table typedefs that drive the interpreter loop. It also details constructor knobs like step limits and debug scripts so embedding tools understand lifecycle expectations. Nested data members describe ownership semantics for modules, runtime strings, and per-function lookup tables. Dependencies include the VM debug and trace headers, IL opcode/type forward declarations, the runtime `rt.hpp` bridge, and standard containers such as `<vector>`, `<array>`, and `<unordered_map>`.

- **src/vm/VMDebug.cpp**

  Implements the debugging hooks that sit inside `VM::handleDebugBreak` and `VM::processDebugControl`. The code coordinates label and source line breakpoints through `DebugCtrl`, honours scripted stepping via `DebugScript`, and enforces global step limits before handing control back to the interpreter loop. When a break fires it logs contextual information, syncs pending block parameters into registers, and returns sentinel slots that pause execution. Dependencies include `vm/VM.hpp`, IL core block/function/instruction headers, the shared `support::SourceManager`, `vm/DebugScript.hpp`, and the standard `<filesystem>`, `<iostream>`, and `<string>` libraries.

- **src/vm/VMInit.cpp**

  Handles VM construction and per-function execution state initialization. The constructor captures module references, seeds lookup tables for functions and global strings, and wires tracing plus debugging facilities provided through `TraceConfig` and `DebugCtrl`. Supporting routines `setupFrame` and `prepareExecution` allocate register files, map block labels, and stage entry parameters so the main interpreter loop can run without additional setup. It depends on `VM.hpp`, IL core structures (`Module`, `Function`, `BasicBlock`, `Global`), runtime helpers like `rt_const_cstr`, and standard containers along with `<cassert>`.


## Tools
- **src/tools/basic-ast-dump/main.cpp**

  Implements the standalone `basic-ast-dump` utility that reads a BASIC source file and prints its abstract syntax tree. The `main` routine validates that exactly one path argument is supplied, loads the file into memory, and registers it with the shared `SourceManager` so locations resolve correctly. It then builds a `Parser`, constructs the AST, and renders it to standard output via `AstPrinter`, making the tool useful for debugging front-end behaviour and producing golden data. Dependencies include `frontends/basic/AstPrinter.hpp`, `frontends/basic/Parser.hpp`, `support/source_manager.hpp`, and standard I/O headers `<fstream>`, `<sstream>`, and `<iostream>`.

- **src/tools/basic-lex-dump/main.cpp**

  Provides the `basic-lex-dump` command-line tool for inspecting how the lexer tokenizes BASIC input. After checking the argument count and loading the requested file, it registers the buffer with `SourceManager`, instantiates `Lexer`, and repeatedly calls `next` until an EOF token is produced. Each token is printed with its line and column plus the lexeme for identifiers, strings, and numbers, allowing developers to build golden token streams when evolving the lexer. Dependencies cover `frontends/basic/Lexer.hpp`, `frontends/basic/Token.hpp`, `support/source_manager.hpp`, and the standard `<fstream>`, `<sstream>`, and `<iostream>` facilities.

- **src/tools/il-dis/main.cpp**

  Acts as a tiny IL disassembler demo that constructs a module in memory and emits it as text. The program uses `il::build::IRBuilder` to declare the runtime `rt_print_str` extern, create a global string, and populate `main` with basic blocks and instructions that print and return zero. Once the synthetic module is built it serializes the result to standard output via the IL serializer, making the example handy for tutorials and smoke tests. Dependencies include `il/build/IRBuilder.hpp`, `il/io/Serializer.hpp`, and `<iostream>`.

- **src/tools/ilc/break_spec.cpp**

  Implements helpers for parsing the `--break` specifications accepted by the `ilc` driver. `isSrcBreakSpec` splits strings on the final colon, ensures the right-hand side contains only digits, and checks that the left-hand side resembles a path so breakpoints map cleanly to files. By rejecting malformed input early it prevents the debugger from enqueuing meaningless breakpoints that would confuse later resolution stages. Dependencies are limited to the local `break_spec.hpp` plus `<cctype>` and `<string>` from the standard library.
- **src/tools/ilc/main.cpp**

  Hosts the entry point for the `ilc` multipurpose driver. `usage` prints the supported subcommands and BASIC guidance, and `main` validates arguments before dispatching to `cmdRunIL`, `cmdILOpt`, or `cmdFrontBasic`. It also lists BASIC intrinsics so users know which builtin names are available when invoking the front-end mode. Dependencies include the local `cli.hpp`, `frontends/basic/Intrinsics.hpp`, and standard `<iostream>`/`<string>` facilities.

- **src/tools/ilc/cmd_run_il.cpp**

  Executes serialized IL modules through the VM while honoring debugging and tracing flags from the CLI. The option parser accepts breakpoints, scripted debug command files, stdin redirection, instruction counting, and timing toggles before loading the module via the expected-based API. After verifying the module it configures `TraceConfig` and `DebugCtrl`, constructs the VM, and prints optional summaries when counters are requested. Dependencies span `vm/Debug.hpp`, `vm/DebugScript.hpp`, `vm/Trace.hpp`, `vm/VM.hpp`, shared CLI helpers, IL API headers, and standard `<chrono>`, `<fstream>`, `<memory>`, `<string>`, `<algorithm>`, `<cstdint>`, and `<cstdio>` utilities.

- **src/tools/ilc/cmd_il_opt.cpp**

  Implements the `ilc il-opt` subcommand that runs transformation passes over IL files. It parses output destinations, comma-separated pass lists, and mem2reg toggles before loading the input through `il::api::v2::parse_text_expected`. Pass registrations wire default and user-selected pipelines into the `transform::PassManager`, and the optimized module is serialized in canonical form. Dependencies cover `il/transform` headers (`PassManager`, `Mem2Reg`, `ConstFold`, `Peephole`, `DCE`), the CLI facade, the IL API and serializer, plus `<algorithm>`, `<fstream>`, `<iostream>`, `<string>`, and `<vector>` utilities.

- **src/tools/ilc/cli.cpp**

  Provides helpers for parsing CLI flags shared across all `ilc` subcommands. `parseSharedOption` recognizes trace mode settings, stdin redirection, maximum step limits, and bounds-check toggles while updating a shared options struct. Its return value lets callers know whether a flag was handled or if they should treat it as an error. Dependencies include `cli.hpp`, which defines `SharedCliOptions` and ties into `il::vm::TraceConfig`, along with the standard string utilities included there.

- **src/tools/ilc/cmd_front_basic.cpp**

  Drives the BASIC front-end workflow for `ilc`, supporting both `-emit-il` compilation and `-run` execution. The helper `compileBasicToIL` loads the source, parses it, folds constants, runs semantic analysis with diagnostics, and lowers the program into IL while tracking source files. Command handling reuses `parseSharedOption`, emits IL text when requested, or verifies and runs the module via the VM. Dependencies include BASIC front-end headers (`Parser`, `ConstFolder`, `SemanticAnalyzer`, `Lowerer`, `DiagnosticEmitter`), the IL expected-based API, serializer, VM runtime headers, and standard `<fstream>`, `<sstream>`, `<iostream>`, and `<string>` facilities.

- **src/tools/il-verify/il-verify.cpp**

  Implements the standalone `il-verify` tool that parses and verifies IL modules from disk. The main routine handles `--version`, checks usage, opens the requested file, and routes diagnostics from the expected-based parse and verify helpers. It prints `OK` on success and returns non-zero when I/O, parsing, or verification fails. Dependencies include `il/api/expected_api.hpp`, `il/core/Module.hpp`, and `<fstream>`, `<iostream>`, `<string>` from the standard library.

## TUI
- **tui/apps/tui_demo.cpp**

  Serves as the sample executable demonstrating how to wire the terminal UI stack together. It inspects the `VIPERTUI_NO_TTY` environment variable to decide whether to run headless, constructs a `TerminalSession`, real terminal IO adapter, theme, and backing text buffer, then builds a widget tree containing a `TextView` and `ListView` joined by an `HSplitter`. The app registers those widgets with the `FocusManager`, primes the event loop, and in interactive mode enters a platform-specific read loop that decodes keystrokes into `ui::Event`s fed to `App`. Execution exits when the user presses Ctrl+Q, making the demo handy for manual testing and screenshots. Dependencies include `tui/app.hpp`, `tui/style/theme.hpp`, `tui/term/input.hpp`, `tui/term/session.hpp`, `tui/term/term_io.hpp`, `tui/text/text_buffer.hpp`, `tui/views/text_view.hpp`, `tui/widgets/list_view.hpp`, `tui/widgets/splitter.hpp`, plus `<cstdlib>`, `<memory>`, `<string>`, `<vector>`, and platform console headers.

- **tui/src/app.cpp**

  Implements the headless `viper::tui::App` loop that coordinates focus, layout, and rendering for the widget hierarchy. The constructor captures the root widget, wraps the provided `TermIO` in the renderer, and pre-sizes the internal `ScreenBuffer` to match the requested terminal dimensions. `tick` drains queued events, handles tab-based focus cycling, consults an optional `input::Keymap`, and forwards remaining events to the focused widget before laying out and painting the tree. After rendering it flushes the buffer through `Renderer::draw`, snapshots the previous frame for diffing, and exposes `resize` so callers can adjust the terminal geometry. Dependencies come from `tui/app.hpp`, which pulls in the UI widget base classes, focus manager, renderer, terminal key event definitions, and screen buffer utilities.

- **tui/src/config/config.cpp**

  Parses the INI-style configuration files that customize themes, key bindings, and editor behaviour. Static helpers trim whitespace, parse hexadecimal colours, interpret key chords, and normalize boolean strings so the loader tolerates user formatting quirks. `loadFromFile` walks the file line by line, tracking the current section to fill theme palettes, append global keymap bindings, and configure editor options like tab width or soft wrapping. Dependencies include `tui/config/config.hpp` along with `<algorithm>`, `<cctype>`, `<fstream>`, `<sstream>`, and `<string_view>`.

- **tui/src/input/keymap.cpp**

  Defines the `Keymap` that maps key chords onto executable commands. It provides comparison and hash operators for `KeyChord`, allowing global and widget-specific bindings to live in unordered maps keyed by modifiers, key codes, and Unicode codepoints. The implementation records command metadata, registers bindings, executes the stored callbacks, and exposes lookup helpers so widgets or palettes can inspect the command list. `handle` checks widget overrides before consulting the global table, returning whether the event was consumed. Dependencies reside in `tui/input/keymap.hpp`, which itself pulls in the UI widget base class and `tui::term::KeyEvent` definitions.

- **tui/src/render/renderer.cpp**

  Implements the ANSI escape renderer that emits minimal terminal updates based on `ScreenBuffer` diffs. `setStyle` lazily writes either 24-bit or 256-colour sequences depending on the true-colour flag while caching the last style to avoid redundant output. `moveCursor` and `draw` cooperate to reposition the terminal cursor, walk changed spans, encode UTF-8 glyphs by hand, and stream them through the borrowed `TermIO` before flushing. Dependencies include `tui/render/renderer.hpp` together with `<string>`, `<string_view>`, and `<vector>`.

- **tui/src/render/screen.cpp**

  Backs the renderer with a `ScreenBuffer` that tracks both the current and previous frame at cell granularity. Equality operators for `RGBA`, `Style`, and `Cell` make diffing straightforward when searching for modified regions. `resize`, `clear`, `snapshotPrev`, and `computeDiff` maintain the double buffer and produce compact spans describing changes so the renderer can minimize writes. Dependencies consist of `tui/render/screen.hpp` and `<algorithm>` for bulk operations.

- **tui/src/style/theme.cpp**

  Provides the default colour theme consumed by widgets and renderers. The constructor seeds normal, accent, disabled, and selection palettes with RGBA values tuned for dark terminals. `style` returns the palette entry matching a requested role so drawing code can translate semantic roles into colours without duplicating tables. Dependencies include `tui/style/theme.hpp` and the render style definitions it exposes.

- **tui/src/syntax/rules.cpp**

  Implements a lightweight syntax highlighter driven by a JSON array of regex rules. An internal `JsonParser` walks the configuration, interpreting each rule's pattern and style block (including colour and bold attributes) into compiled `std::regex` objects and `render::Style` values. `SyntaxRuleSet::spans` caches the last highlighted text for each line to avoid recomputation until edits invalidate the entry. Dependencies include `tui/syntax/rules.hpp`, `<cctype>`, `<fstream>`, `<sstream>`, and the rendering and regex support provided through that header.

- **tui/src/term/clipboard.cpp**

  Offers clipboard implementations that rely on OSC 52 escape sequences to copy text from the terminal. Helper routines perform base64 encoding, honour the `VIPERTUI_DISABLE_OSC52` environment flag, and assemble the precise control strings terminals expect. `Osc52Clipboard` writes the encoded payload through a `TermIO` adapter and flushes it, while `MockClipboard` records the sequence and decodes it back to text for tests. Dependencies include `tui/term/clipboard.hpp`, `tui/term/term_io.hpp`, and standard `<cstdlib>`, `<string>`, and `<string_view>` utilities.

- **tui/src/term/input.cpp**

  Translates raw terminal bytes into structured key and mouse events through the `InputDecoder` state machine. The decoder buffers partial UTF-8 sequences, recognises CSI escape patterns, and understands bracketed paste and SGR mouse encodings so higher-level code receives meaningful events. Helper functions parse numeric parameters, derive modifier masks, and enqueue `KeyEvent` or `MouseEvent` objects for later consumption. Dependencies come from `tui/term/input.hpp` alongside `<string_view>` and the container types supplied via the header.

- **tui/src/term/session.cpp**

  Wraps terminal initialisation in an RAII `TerminalSession` that toggles raw mode, alternate screens, and optional mouse reporting. Construction checks environment toggles, consults `isatty`/`tcgetattr` on POSIX or `GetConsoleMode` on Windows, and uses `RealTermIO` to write the escape sequences that prepare the host terminal. The destructor reverses those changes—disabling mouse tracking, leaving the alternate screen, restoring the cursor, and reapplying saved terminal attributes—only when initialisation succeeded. Dependencies include `tui/term/session.hpp`, `tui/term/term_io.hpp`, and the platform headers pulled in transitively.

- **tui/src/text/text_buffer.cpp**

  Implements a piece-table `TextBuffer` supporting efficient insertions, deletions, undo/redo, and line lookups for the editor. `load` seeds the original buffer and line index, while `insert` and `erase` split pieces, update size counters, maintain the line-start table, and record operations into grouped transactions. `beginTxn`, `endTxn`, and query helpers such as `getText` rebuild substrings on demand without copying stored text, keeping edits and history compact. Dependencies include `tui/text/text_buffer.hpp` together with `<algorithm>` and `<cassert>`; the header supplies the container types used here.

- **tui/src/ui/widget.cpp**

  Provides the default behaviour for the abstract `ui::Widget` base class. It caches the rectangle passed to `layout`, implements `paint` and `onEvent` as no-ops to be overridden by concrete widgets, and reports that widgets do not accept focus unless they explicitly opt in. Additional helpers expose the stored rectangle and ignore focus-change notifications, keeping the base contract minimal. Dependencies are limited to `tui/ui/widget.hpp`, which defines the widget interface and supporting structures.

- **tui/src/views/text_view.cpp**

  Implements the primary text editor view that renders a `TextBuffer`, tracks selection state, and responds to navigation commands. It translates between byte offsets and screen columns using UTF-8 decoding plus `util::char_width`, clamps cursor movement, and maintains scroll offsets so the caret stays visible. The painting routine draws an optional gutter, applies syntax highlight spans, and writes selection or highlight styles into the `ScreenBuffer` using the injected theme. Dependencies span `tui/views/text_view.hpp`, `tui/render/screen.hpp`, `tui/syntax/rules.hpp`, and standard `<algorithm>` and `<string>` helpers.

- **tui/src/widgets/command_palette.cpp**

  Defines the command palette widget that lets users search and run registered keymap commands. It keeps a lowercase query string, rebuilds the filtered command ID list on every change, and captures focus so keystrokes immediately update the palette. Event handling consumes backspace, printable characters, and enter to trigger the top match, while `paint` draws the prompt and visible results into the `ScreenBuffer`. Dependencies include `tui/widgets/command_palette.hpp`, `tui/render/screen.hpp`, and the keymap facilities provided through the header.
