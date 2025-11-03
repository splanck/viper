# CODEMAP: Front End (BASIC)

- **src/frontends/basic/AST.cpp**

  Defines the visitor `accept` overrides for every BASIC expression and statement node so passes can leverage double-dispatch instead of manual `switch` ladders. Each override simply forwards `*this` to the supplied `ExprVisitor` or `StmtVisitor`, preserving the tree's ownership model while letting the visitor decide behaviour. Keeping the logic centralized in one translation unit gives future extensions a stable home for additional helper code or instrumentation. Dependencies are limited to `frontends/basic/AST.hpp`, which declares the node types and visitor interfaces.

- **src/frontends/basic/AST.hpp**

  Declares the BASIC front-end abstract syntax tree covering all expression and statement variants emitted by the parser. Nodes record `SourceLoc` metadata, expose owned children via `std::unique_ptr`, and implement `accept` hooks so visitors can traverse without type introspection. Enumerations such as the builtin type tags and binary operator list capture language semantics for later passes. Dependencies include `support/source_location.hpp` alongside standard containers `<memory>`, `<string>`, and `<vector>`.

- **src/frontends/basic/AstPrinter.cpp**

  Renders BASIC ASTs into s-expression-style strings for debugging tools and golden tests. Nested `ExprPrinter` and `StmtPrinter` visitors walk every node type and stream formatted output through the shared `Printer` helper to keep formatting centralized. Formatting logic normalizes numeric output, resolves builtin names, and emits statement punctuation so the textual tree mirrors parser semantics. Dependencies include `frontends/basic/AstPrinter.hpp`, `frontends/basic/BuiltinRegistry.hpp`, and `<array>`/`<sstream>` for lookup tables and temporary buffers.

- **src/frontends/basic/BasicCompiler.cpp**

  Coordinates the front-end pipeline that turns a BASIC buffer into an IL module by chaining parsing, constant folding, semantic analysis, and lowering. It provisions a `DiagnosticEmitter` backed by the shared `SourceManager`, registers the source text, and ensures each phase stops on the first unrecoverable error before continuing. File identifiers are resolved for in-memory or on-disk inputs so diagnostics have stable provenance, and lowering honours bounds-check options before returning the finished module. Dependencies include `frontends/basic/BasicCompiler.hpp`, `frontends/basic/Parser.hpp`, `frontends/basic/ConstFolder.hpp`, `frontends/basic/SemanticAnalyzer.hpp`, `frontends/basic/Lowerer.hpp`, and `support::SourceManager`.

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

- **src/frontends/basic/LoweringPipeline.cpp**

  Implements the helper visitors and utilities `Lowerer` uses to stage BASIC lowering in well-defined passes. The expression and statement collectors walk AST nodes to mark referenced symbols, arrays, and builtin usages while deferring to `Lowerer` for bookkeeping. Additional helpers translate BASIC scalar types to IL core kinds and ensure array metadata is recorded before emission so later passes see consistent slot information. Dependencies include `frontends/basic/LoweringPipeline.hpp`, `frontends/basic/Lowerer.hpp`, `frontends/basic/TypeSuffix.hpp`, `viper/il/IRBuilder.hpp`, and IL core block/function definitions.

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

- **src/frontends/basic/TypeSuffix.cpp**

  Offers the tiny helper that infers a BASIC identifier’s type from its trailing sigil so semantic analysis and lowering agree on default numeric kinds. The routine checks the final character, mapping `$` to strings, `#` to double-precision values, and falling back to 64-bit integers when no suffix is present. Keeping the logic centralized avoids diverging suffix rules across the lexer, analyzer, and lowering pipeline. Dependencies are limited to `frontends/basic/TypeSuffix.hpp` and the standard `<string_view>` facilities it includes.

- **src/frontends/basic/Token.cpp**

  Implements the token kind to string mapper used by BASIC lexer and tooling diagnostics to render human-readable token names. The switch enumerates every `TokenKind`, covering keywords, operators, and sentinel values so debuggers and golden tests stay in sync with the parser. Because the function omits a default case it triggers compiler warnings whenever new token kinds are introduced, keeping the mapping complete over time. Dependencies are limited to `frontends/basic/Token.hpp` and the standard language support already included there.
