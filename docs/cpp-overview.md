# C++ Project Overview

Here’s a high-level but extensive blueprint for turning the IL + VM + codegen + front end
design into a clean, maintainable C++20 project. It’s written to be “handable” to an
assistant like Codex and to keep you out of the mess that VC drifted into.

1. Guiding principles

- Single thin‑waist IL: all front ends target it; both the VM and codegen consume it.
- Small core, strong boundaries: each layer compiles to its own library; no cross‑layer reach‑through.
- Determinism first: the VM is the semantic oracle; native code must match it.
- Testable units: verifier, serializer, VM, and backend have dedicated golden/e2e tests.
- Solo‑friendly: minimal dependencies; predictable build; clear incremental milestones.

1. Repository layout (CMake, modular)

/CMakeLists.txt
/cmake/ # compiler flags, toolchain helpers
/docs/ # IL spec, developer docs, ADRs
/runtime/ # C runtime (librt.a): rt\__.c, rt\__.hpp
/src/
/support/ # small utilities shared across libs
/il/ # IL core: types, IR, builder, verifier, I/O
/vm/ # IL interpreter
/codegen/
/x86_64/ # x86-64 SysV backend
/frontends/
/basic/ # BASIC lexer, parser, AST, lowering
/tools/ # CLI tools: ilc, il-dis, il-verify
/tests/
/unit/ # gtest/catch2 unit tests
/golden/ # text-based golden tests (IL, diagnostics)
/e2e/ # compile & run comparisons (VM vs native)
/scripts/ # dev scripts (format, lint, build, test)
Top-level CMake targets

- il_core (static lib)
- il_vm (static lib)
- il_codegen_x86_64 (static lib)
- frontend_basic (static lib)
- librt (static lib from /runtime)
- ilc, il-dis, il-verify (executables)
- tests\_\* (test executables)

3. Tooling & Build

- C++ standard: C++20 (-std=c++20)
- Warnings: -Wall -Wextra -Wpedantic -Werror
- Sanitizers (dev/CI): -fsanitize=address,undefined
- Dependencies (small, vendorable):
  ○ fmt (formatted printing)
  ○ CLI11 or lyra (CLI parsing), or roll a tiny one
  ○ gtest or Catch2 (tests)
- Style: clang-format, clang-tidy with a minimal config
- CI: GitHub Actions matrix (Linux, macOS), caches build, runs sanitizers + tests

1. Namespaces and libraries

- il::core — types, values, instructions, module, symbol tables
- il::build — IRBuilder helpers
- il::io — text parser/serializer
- il::verify — structural/type verifier
- il::vm — interpreter engine
- il::codegen::x86_64 — instruction selection, regalloc, asm emitter
- fe::basic — BASIC front end (lexer/parser/AST/lowering)
- rt — C ABI runtime (no namespace; C headers)

1. Core IL library (C++)
   5.1 Data model (lightweight, cache‑friendly)

- Type system

enum class TypeKind { Void, I1, I64, F64, Ptr, Str };
struct Type { TypeKind kind; /\* eq, hash \*/ };

- Value hierarchy (flat, tagged)

enum class ValueKind { Temp, ConstInt, ConstFloat, ConstStr, GlobalAddr };
struct Value { ValueKind kind; Type type; uint32_t id; /\* payload via union \*/ };

Rationale: a small tagged struct beats deep inheritance for speed and simplicity.

- Instruction

enum class Opcode { Add, Sub, Mul, SDiv, UDiv, ... , Load, Store, Call, Br, CBr, Ret, Alloca, GEP, ... };
struct Instr {
Opcode op;
Type type; // result type if any (void for stores, branches)
Value dst; // optional (invalid for void)
small_vector\<Value, 4> ops; // operands
SourceLoc loc; // optional metadata
};

- BasicBlock / Function / Module

struct BasicBlock { Symbol name; std::vector<Instr> instrs; };
struct Param { Symbol name; Type type; };
struct Function {
Symbol name; std::vector<Param> params; Type ret;
std::vector<BasicBlock> blocks; AttrMask attrs; Visibility vis;
SymbolTable locals; // temps, slots if named
};
struct Global { Symbol name; Type type; GlobalInit init; bool isConst; Visibility vis; };
struct Module { Target triple; vector<ExternDecl> externs; vector<Global> globals; vector<Function> funcs; StrTable strlits; };

- Symbol table & interning
  ○ Symbol is an interned string handle (uint32 id → string table).
  ○ All names (@main, labels, params) live in a per‑module interner.
- Memory management
  ○ Use arenas (monotonic buffers) for IR nodes; IR is long‑lived and mostly append‑only.
  5.2 IRBuilder (ergonomic construction)
- Fluent helpers that guarantee well‑formedness locally:

class IRBuilder {
Function\* f\_;
BasicBlock\* bb\_;
// ...
Value const_i64(int64_t);
Value add(Value a, Value b);
void br(Label dst);
void cbr(Value cond, Label t, Label f);
// ...
};

- Enforces “exactly one terminator per block”, operand typing, and records source locations from front ends.
  5.3 IL I/O (text format)
- Serializer: pretty, deterministic (sorted externs/globals, stable temp numbering).
- Parser: Pratt‑style for expressions inside instructions isn’t needed; use a small hand‑rolled tokenizer and recursive descent for the grammar in your spec.
- Round‑trip tests: parse→print→parse equivalence (modulo whitespace).
  5.4 Verifier
- Structural checks (one terminator per block; labels defined/used).
- Type checks (operand and result types per opcode).
- Call signature checks.
- Memory ops alignment rules (load/store size vs natural alignment).
  Deliverable: il_verify(Module&) -> std::vector<Diagnostic>.

6. Runtime library (/runtime, C, stable ABI)

- Files: rt_print.c, rt_string.c, rt_input.c, rt_mem.c, rt_math.c
- Headers: rt.hpp with declarations:

void rt_print_str(rt_str s);
void rt_print_i64(int64_t v);
void rt_print_f64(double v);
rt_str rt_input_line(void);
int64_t rt_len(rt_str s);
rt_str rt_concat(rt_str a, rt_str b);
// ...
void\* rt_alloc(int64_t bytes); // simple wrapper on malloc for v1

- String representation: ref‑counted heap blocks (len + capacity + UTF‑8 bytes).
- Build: static library librt.a, linked by native binaries; callable from VM through host‑function table.

7. Interpreter (il::vm)
   7.1 Execution engine

- VM types

struct Slot { uint64_t u64; double f64; void\* ptr; /\* tagged by Type at use _/ };
struct Frame {
Function_ f;
std::vector<Slot> regs; // virtual regs / temps
std::vector\<uint8_t> stack; // for alloca
size_t ip_block, ip_index; // current block, instr index
};
struct VM {
Module\* m;
HostFuncs host; // C ABI shims to runtime
std::vector<Frame> callstack;
};

- Dispatch: classic switch(opcode) on Instr; computed goto optional later.
- Alloca: bump pointer in Frame::stack, zero‑initialized.
- Load/store: check null + alignment, then memcpy by size (8 for i64/f64).
- Calls: if callee is IL function → push new frame; if extern → call host C function with marshalled args and result.
- Traps: throw VMTrap (or return error) with location; top‑level converts to diagnostic + non‑zero exit.
  7.2 Tracing & diagnostics
- --trace: dump func:label:instr and value states.
- --trace-calls: show call/ret push/pop frames.
- Include source metadata from IL when available.

8. Codegen (il::codegen::x86_64)
   8.1 Pipeline
   1. Lowering: turn IL instructions into a simple target‑agnostic MIR (optional) or go direct.
   1. Liveness: compute live intervals of virtual regs within each function (linear time scan).
   1. Regalloc (linear scan):
      ○ Register classes: GP (RAX…R15), FP (XMM0…XMM15).
      ○ Spill to fixed stack slots; reuse spills when possible.
   1. Instruction selection: greedy patterns:
      ○ add i64 → addq
      ○ compares → cmp + setcc/cmovcc/conditional branches
      ○ load/store → mov with [base + disp]
   1. Prologue/Epilogue:
      ○ Preserve RBX, RBP, R12–R15 if used.
      ○ 16‑byte stack alignment at call sites.
   1. Calling convention (SysV x86‑64):
      ○ Int/pointers: RDI, RSI, RDX, RCX, R8, R9; floats: XMM0–7.
      ○ Return: RAX (int/pointer) or XMM0 (float).
      ○ i1 zero‑extended in EDI/EAX when passed/returned.
   1. Asm emitter:
      ○ Output .text, .globl, labels, comments with IL source refs.
      ○ Choice of AT&T vs Intel syntax via flag (default AT&T on \*nix).
      8.2 Toolchain integration

- Assembler/linker: call cc/clang to assemble .s → .o and link with librt.a.
- Output: a.out by default or -o <path>.
- Windows: add MS x64 or SysV for mingw in a later milestone.

1. BASIC front end (fe::basic)
   9.1 Lexer (hand‑rolled)

- Tokens: identifiers (A‑Z$, %), numbers (ints, floats), strings ("..."), keywords (PRINT, LET, IF, THEN, ELSE, WHILE, WEND, GOTO, GOSUB, RETURN, END), newlines/line numbers.
- Keep track of line labels (10, 20, …) → map to synthetic labels in IL.
  9.2 Parser
- Approach: recursive descent with a Pratt expression parser for precedence.
- AST: small node set (Expr, Stmt subclasses).
- Desugaring:
  ○ ELSEIF → IF nesting
  ○ WHILE/WEND → blocks and conditional branches
  ○ 1‑based string indexes → 0‑based IL/runtime calls
  9.3 Semantic analysis
- Symbol table for variables (global vs function‑local).
- Type coercions: numeric to i64/f64, string ops via runtime.
- Built‑ins mapping to runtime calls (e.g., PRINT routes to rt_print\_\*).
  9.4 Lowering to IL
- One IRBuilder instance per function.
- Variables → alloca 8 in entry + load/store.
- Branching and labels: maintain a map from BASIC line numbers to IL labels.
- Attach SourceLoc to each emitted instruction for diagnostics.

1. CLI tools (/src/tools)

- ilc: the driver
  ○ ilc front basic file.bas -emit-il → stdout IL
  ○ ilc front basic file.bas -run → VM execution
  ○ ilc front basic file.bas -S → emit assembly
  ○ ilc front basic file.bas -o a.out → compile & link
  ○ --trace, --verify
- il-verify: reads IL, runs verifier, prints diagnostics.
- il-dis: pretty prints IL (for .il or binary form if added later).

1. Testing strategy

- Unit tests:
  ○ Types, symbol tables, IRBuilder basic use.
  ○ Verifier: valid/invalid cases per rule.
  ○ VM arithmetic/branches/calls/traps.
  ○ Codegen snippets (e.g., add, cmp+jcc, call sequences) with assembler roundtrip.
- Golden tests:
  ○ BASIC → expected IL text (stable names/labels).
  ○ IL → expected pretty‑printed IL (round‑trip).
- End‑to‑end:
  ○ Run IL on VM vs compiled native; compare stdout and exit code.
  ○ Include edge cases: INT64_MIN/-1, div 0 trap, fptosi NaN trap.
- Fuzz (lightweight):
  ○ Randomized expressions in BASIC; ensure VM and native match.

1. Diagnostics & developer UX

- Diagnostic object

struct Diagnostic { Severity sev; std::string msg; SourceLoc loc; };

- Pretty printer: show source lines with carets; color in TTY.
- Trace modes: --trace, --trace-calls, --trace-regalloc (backend), --trace-asm.

13. Performance & memory

- Arenas for IR nodes and string interning → low allocation overhead.
- VM fast path:
  ○ Cache frequently used runtime function pointers.
  ○ Optional direct‑threaded dispatch (compile‑time flag).
- Backend:
  ○ Peephole pass to remove redundant moves and combine cmp/jcc.
  ○ Linear‑scan with interval splitting once basic is stable.

1. Portability plan

- Phase 1: Linux/macOS x86‑64 SysV.
- Phase 2: Windows x64 (different calling convention + prologue rules).
- Phase 3: ARM64 (Apple M‑series + Linux aarch64) with a sibling backend.

1. Documentation & governance

- /docs/references/il.md — the spec you wrote (versioned, with change log).
- /docs/adr/ — short Architecture Decision Records (e.g., “no φ in v0.1”).
- /docs/dev/ — build/run instructions, backend ABI notes, coding standards.
- Auto‑generated API docs with Doxygen (optional).

1. Milestones (project plan)
   M0 — Scaffolding (1–2 days)

- CMake skeleton, il_core stubs, librt hello‑world, CI, clang‑format.
  M1 — IL Core + Verifier
- Types/values/instructions, builder, serializer/parser, verifier, unit tests.
  M2 — VM
- Minimal interpreter executing arithmetic, branches, calls; host C runtime bridge; tracing.
  M3 — BASIC Front End (subset)
- Lexer/parser for LET, PRINT, IF/THEN/ELSE, GOTO, WHILE/WEND.
- Lowering to IL with source locations.
- Golden tests (BASIC→IL) + VM e2e.
  M4 — Codegen x86‑64 v1
- Greedy selection, simple linear‑scan, prologue/epilogue, runtime calls, link.
- Differential tests (VM vs native) for the conformance suite.
  M5 — Quality pass
- Peephole fixes, better diagnostics, more runtime funcs (strings/math), docs polish.

1. Extension hooks

- Pass framework: add a tiny pass manager (il::pass::run(Module&, PassPipeline)), even if you only do verify and peephole initially.
- Plugin points:
  ○ New front ends register with ilc via a factory.
  ○ Backends: il::codegen::Backend interface so ARM64 can slot in later.
- Optional SSA: add an “IL→SSA→IL” pass later if you want classic data‑flow opts.

1. Risk controls (to avoid VC‑style sprawl)

- Keep runtime and compiler strictly separate; communicate only over the C ABI.
- No front end is allowed to emit target‑specific hints; everything goes through IL.
- Verifier is always run in debug builds (and via ilc --verify).
- Each new feature ships with:
  1\. unit test(s),
  2\. golden IL (if front end),
  3\. VM vs native differential test.

1. What to implement first (files checklist)

- /src/il/Type.hpp/.cpp — Type, equality, helpers
- /src/il/IR.hpp/.cpp — Value, Instr, BasicBlock, Function, Module
- /src/il/IRBuilder.hpp/.cpp — helpers
- /src/il/Serialize.hpp/.cpp — text I/O
- /src/il/Verify.hpp/.cpp — verifier
- /src/vm/VM.hpp/.cpp — engine, run(Module&, entry)
- /runtime/rt.hpp, rt_print.c, rt_string.c, rt_input.c — minimal runtime
- /src/tools/ilc.cpp — driver with -emit-il and -run
- Tests: unit + two e2e samples (e.g., “HELLO”, “sum 1..10”)
  This gives you a walking skeleton: end‑to‑end from BASIC → IL → VM run, with verify and text I/O, ready for codegen to slot in.

Closing thought
This structure keeps each layer crisp and independently testable. You can develop methodically: lock IL + verifier, bring up the VM (semantic oracle), then add the BASIC front end (golden tests), and finally land x86‑64 codegen with differential tests. Each milestone is small enough for a single dev and friendly to a coding assistant working off this plan.
