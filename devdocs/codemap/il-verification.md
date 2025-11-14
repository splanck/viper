# CODEMAP: IL Verification

- **src/il/verify/ControlFlowChecker.cpp**

  Handles verifier checks specific to IL control flow, ensuring blocks and terminators obey structural rules. Helpers like `validateBlockParams_E` seed type information for block parameters, `iterateBlockInstructions_E` walks instructions until the terminator, and `checkBlockTerminators_E` enforces the single-terminator rule from the IL spec. Branch utilities validate jump targets, branch argument counts, and type compatibility while cross-referencing maps of reachable blocks, externs, and functions. Dependencies include `il/verify/ControlFlowChecker.hpp`, `il/verify/TypeInference.hpp`, IL core containers (`BasicBlock`, `Instr`, `Function`, `Extern`, `Param`), `support/diag_expected.hpp`, and standard `<functional>`, `<sstream>`, `<string_view>`, and `<unordered_set>` facilities.

- **src/il/verify/InstructionChecker.cpp**

  Provides verifier checks for non-control-flow IL instructions by pairing opcode metadata with type inference. `verifyOpcodeSignature_E` enforces operand/result counts and successor arity from `OpcodeInfo`, while targeted helpers validate allocas, memory operations, arithmetic, and runtime calls. Diagnostics format instruction snippets, queue optional warnings, and power both streaming and `Expected`-based verifier entry points so tooling can choose its preferred API. Dependencies include `il/verify/InstructionChecker.hpp`, `il/verify/TypeInference.hpp`, IL core types (`Function`, `BasicBlock`, `Instr`, `Extern`, `OpcodeInfo`), and `support/diag_expected.hpp` together with `<sstream>`, `<string_view>`, `<unordered_map>`, and `<vector>` from the standard library.

- **src/il/verify/TypeInference.cpp**

  Provides the verifier's type-inference engine for IL, backing operand validation and diagnostic rendering. Construction ties the helper to caller-owned maps and sets so it can track temporary types, mark definitions, and uphold the invariant that every defined id has an associated type. Utility methods render single-line instruction snippets, compute primitive widths, ensure operands are defined, and surface failures as either streamed diagnostics or `Expected` errors. The implementation includes `il/verify/TypeInference.hpp`, draws on IL core instruction and value metadata, and uses `support/diag_expected.hpp` plus `<sstream>` and `<string_view>` to produce rich error text.

- **src/il/verify/TypeInference.hpp**

  Defines the `TypeInference` helper interface that the verifier uses to reason about IL operand types. It offers queries for value types and byte widths along with mutation hooks to record or drop temporaries as control flow progresses. Both streaming and `Expected`-returning verification APIs share the same backing state, giving callers flexibility without duplicating logic. Dependencies span IL core headers for types, values, and forward declarations together with `<unordered_map>`, `<unordered_set>`, `<ostream>`, and `<string>` from the standard library.

- **src/il/verify/DiagSink.cpp**

  Implements the `CollectingDiagSink` used by verifier components to accumulate diagnostics while walking a module. Each reported `Diag` is appended in arrival order so callers can replay or transform the messages after verification completes. The sink also exposes `clear` and read-only accessors, making it a reusable staging buffer for both Expected-based and streaming error paths. Dependencies include `il/verify/DiagSink.hpp` and the diagnostic primitives in `il::support`.

- **src/il/verify/ExternVerifier.cpp**

  Validates that all extern declarations in a module are unique and align with the runtime’s ABI signatures. The verifier interns each extern in a name-to-pointer map, reporting duplicates and augmenting messages when signatures disagree. When a declared extern maps to a known runtime helper it cross-checks parameter and return types so the VM bridge can safely invoke it later. Dependencies include `il/verify/ExternVerifier.hpp`, `il/core/Module.hpp`, `il/core/Extern.hpp`, and the runtime signature table in `il/runtime/RuntimeSignatures.hpp` with `<sstream>` for diagnostics.

- **src/il/verify/FunctionVerifier.cpp**

  Coordinates per-function verification by enforcing entry-block naming, unique labels, consistent signatures, and block parameter invariants before checking each instruction. Strategy objects dispatch control-flow opcodes to dedicated validators while the default strategy leverages `InstructionChecker` and `TypeInference` to ensure operand and result types match opcode contracts. The verifier builds lookup tables for externs, functions, and blocks so branch targets and call sites can be validated without repeated scans, and it emits detailed snippets when checks fail. Dependencies include `il/verify/FunctionVerifier.hpp`, `il/verify/ControlFlowChecker.hpp`, `il/verify/InstructionChecker.hpp`, `il/verify/TypeInference.hpp`, IL core headers (`Module`, `Function`, `BasicBlock`, `Instr`, `Type`, `Extern`), and `<unordered_map>`/`<unordered_set>` containers.

- **src/il/verify/GlobalVerifier.cpp**

  Ensures module-level globals have unique names while building a lookup table that later verification stages can query. The pass scans every `Global`, records duplicates as immediate errors, and retains pointers into the module so consumers avoid repeated linear searches. Because it owns only stable references the verifier introduces no additional lifetime constraints on the module. Dependencies include `il/verify/GlobalVerifier.hpp`, `il/core/Module.hpp`, and `il/core/Global.hpp`.

- **src/il/verify/Verifier.cpp**

  Validates whole modules by checking extern/global uniqueness, building block maps, and dispatching per-instruction structural and typing checks. It orchestrates control-flow validation, opcode contract enforcement, and type inference while collecting both hard errors and advisory diagnostics. Runtime signatures from the bridge are cross-checked against declared externs, and helper functions iterate each block to ensure terminators and branch arguments are well-formed. The verifier depends on `Verifier.hpp`, IL core data structures, the runtime signature catalog, analysis helpers (`TypeInference`, `ControlFlowChecker`, `InstructionChecker`), and the diagnostics framework (`support/diag_expected`).

- **src/il/verify/Verifier.hpp**

  Declares the top‑level verification APIs exposed to tools and passes. Provides both streaming and Expected‑based entry points and documents invariants on module structure and typing the verifier enforces.

- **src/il/verify/FunctionVerifier.hpp**

  Declares per‑function verification routines that coordinate block checks, label uniqueness, parameter arity, and instruction validation within a single function.

- **src/il/verify/BranchVerifier.cpp**, **src/il/verify/BranchVerifier.hpp**

  Validates branch successors and argument counts, ensuring block parameters match caller‑supplied arguments and that all targets exist. Exposes helpers to check individual instructions and whole functions.

- **src/il/verify/OperandCountChecker.cpp**, **src/il/verify/OperandCountChecker.hpp**

  Enforces per‑opcode operand count constraints using metadata from `OpcodeInfo`. Reports precise diagnostics when instructions provide too few or too many operands.

- **src/il/verify/OperandTypeChecker.cpp**, **src/il/verify/OperandTypeChecker.hpp**

  Enforces operand type categories per opcode, leveraging `TypeInference` to query actual operand types and verifying they satisfy required categories (integer, float, pointer, etc.).

- **src/il/verify/ResultTypeChecker.cpp**, **src/il/verify/ResultTypeChecker.hpp**

  Checks that instruction result types match opcode contracts and derived operand types, producing clear diagnostics on mismatches.

- **src/il/verify/InstructionChecker_Arithmetic.cpp**

  Implements arithmetic and comparison instruction checks beyond structural validation, including overflow and division‑by‑zero preconditions where specified by the IL.

- **src/il/verify/InstructionChecker_Memory.cpp**

  Verifies memory instructions (`alloca`, `load`, `store`, `gep`) for type correctness, alignment assumptions, and pointer provenance invariants.

- **src/il/verify/InstructionChecker_Runtime.cpp**

  Validates runtime call sites against declared externs and the runtime signature catalog, checking argument arity/types and result compatibility.

- **src/il/verify/InstructionCheckerShared.hpp**

  Declares shared helpers and small utilities reused across instruction checker translation units.

- **src/il/verify/InstructionCheckUtils.cpp**, **src/il/verify/InstructionCheckUtils.hpp**

  Utility functions for rendering instruction snippets, collecting uses, and other checker support tasks to keep individual checkers focused.

- **src/il/verify/EhModel.cpp**, **src/il/verify/EhModel.hpp**

  Captures the verifier’s model of exception handling constructs (try, catch, finally) so EH‑related checks can reason about permitted instruction sequences and nesting.

- **src/il/verify/EhChecks.cpp**, **src/il/verify/EhChecks.hpp**

  Implements verification of exception‑handling rules: balanced try/catch, legal control transfers, and result/operand typing inside protected regions.

- **src/il/verify/ExceptionHandlerAnalysis.cpp**, **src/il/verify/ExceptionHandlerAnalysis.hpp**

  Performs targeted analysis over functions to annotate blocks with exception handler metadata used by EH verifiers.

- **src/il/verify/GlobalVerifier.cpp**, **src/il/verify/GlobalVerifier.hpp**

  Ensures globals are unique and records a map for fast lookup by later checks.

- **src/il/verify/ExternVerifier.cpp**, **src/il/verify/ExternVerifier.hpp**

  Checks extern declaration uniqueness and alignment with runtime signatures; cross‑references the signature table for known helpers.

- **src/il/verify/Rule.hpp**

  Declares small rule descriptors and tags that help structure per‑opcode verification logic.

- **src/il/verify/SpecTables.hpp**

  Declares tables derived from the IL spec that drive verification (operand categories, result types, etc.). Used by generated sources and checkers.

- **src/il/verify/VerifierTable.cpp**, **src/il/verify/VerifierTable.hpp**

  Materializes and declares the consolidated verifier rule table mapping opcodes to composed checks. Generated or programmatically built from spec tables and hand‑written rules.

- **src/il/verify/DiagFormat.cpp**, **src/il/verify/DiagFormat.hpp**

  Unified diagnostic formatting helpers for the verifier: renders instruction snippets, block/function context, and spec‑driven messages.

- **src/il/verify/DiagSink.hpp**

  Declares the collecting sink used to aggregate diagnostics during verification passes, pairing with the implementation already documented.

- **src/il/verify/VerifyCtx.hpp**

  Declares the data structure passed through checkers carrying maps, type inference state, and configuration knobs for the verification run.

- **src/il/verify/generated/SpecTables.cpp**

  Generated source defining spec‑driven tables for verifier rules and categories. Consumers should not modify by hand; updated via spec tooling.

- **src/il/verify/InstructionChecker.hpp**

  Declares the main instruction checker interface used across the verifier.

- **src/il/verify/InstructionStrategies.cpp**, **src/il/verify/InstructionStrategies.hpp**

  Strategy helpers used by the verifier to route checks per opcode family.

- **src/il/verify/ControlFlowChecker.hpp**

  Header for the control‑flow checker implementation.

- **src/il/verify/EhVerifier.cpp**, **src/il/verify/EhVerifier.hpp**

  EH verifier front door and declaration.
