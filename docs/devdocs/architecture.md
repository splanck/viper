---
status: active
audience: public
last-verified: 2026-02-17
---

# Viper Architecture Overview

**Purpose:** This document explains how Viper compiles and runs programs end-to-end: front ends (Zia, BASIC) →
intermediate language (IL) → optimization passes → execution on the VM interpreter → native code generation. It merges
prior overview notes and archived blueprints so contributors have a single map to the system; deep dives live in linked
pages.

If you're new to the IL, start with the [IL Quickstart](../il-quickstart.md).

## Project goals

- Multi-language front ends (Zia, BASIC) that all lower to a common IL "thin waist."
- Interpreter backend that executes IL directly for fast bring-up, tests, and debugging.
- Native backends that translate IL to assembly (x86-64 SysV and ARM64 AAPCS64), assembled and linked into runnable
  binaries.
- Small, solo-friendly codebase with clear module boundaries, strong tests, and a documented runtime ABI (versioned;
  evolving).

## High-level pipeline

The core stages and artifacts:

- **Front ends (Zia, BASIC):** lex/parse → AST → semantic analysis (types, arity, lvalues).
- **Lowering:** AST → IL module (functions, blocks, instructions).
- **Optimization passes:** constant folding, dead code elimination, peephole rewriting.
- **Execution backends:**
    - **VM interpreter** — primary development/debugging target.
    - **Code generation** — AArch64 validated (Apple Silicon); x86_64 implemented and validated.

```text
+-----------------------+        +-----------------------+
|   Frontends (2)       |        |        Tools          |
|  - Zia, BASIC         |        |  - CLI (driver)       |
|                       |        |  - IL verifier        |
+-----------+-----------+        |  - Disassembler       |
            |                    +-----------+-----------+
            v                                |
   +-----------------------+                 |
   |      IL Builder       |                 |
   +-----------+-----------+                 |
               |                             |
      +--------v---------+          +--------v---------+
      |   IL Optimizer   |  (opt)   |   IL Serializer  |
      |  (optional)      +---------->  (.il text/bc)   |
      +--------+---------+          +--------+---------+
               |                             |
               +-----------------------------+
               |
   +-----------v----------+        +---------------------+
   |       IL VM          |        |   Codegen Backend   |
   |    (Interpreter)     |        |    (IL → Assembly)  |
   +----------------------+        +---------------------+
```

All languages feed the same IL builder, and both the VM and code generator consume that shared representation, keeping
the IL as the thin waist between language-specific semantics and machine execution.

### End-to-end lifecycle

`viper` is the command-line entry point. It parses arguments, loads source files, and drives the compile and execute
pipeline:

1. BASIC front end emits IL.
2. `PassManager` applies optimizations.
3. Verifier checks invariants.
4. VM loads the module and runs `main`.

```sh
$ viper run examples/basic/ex1_hello_cond.bas
HELLO
READY
10
10
```

When the native backend is enabled, the same IL feeds the code generator instead of the interpreter.

## Source layout (where things live)

- **Build system:** top-level `CMakeLists.txt`, `cmake/` helpers, `scripts/` for dev automation.
- **Code generation:** `src/codegen/` (aarch64, common, x86_64).
- **Docs & examples:** `docs/`, `examples/`.
- **Front ends:** `src/frontends/basic/`, `src/frontends/common/`, `src/frontends/zia/`.
- **IL core:** `src/il/analysis/`, `src/il/api/`, `src/il/build/`, `src/il/core/`, `src/il/internal/`, `src/il/io/`, `src/il/runtime/`, `src/il/utils/`, `src/il/verify/`.
- **Passes:** `src/il/transform/`.
- **Support utilities:** `src/support/`, `src/common/`.
- **Tests:** `src/tests/` (`unit/`, `golden/`, `e2e/`, `smoke/`, `perf/`).
- **Tools:** `src/tools/viper/` (driver and subcommands), `src/tools/`.
- **VM:** `src/vm/`, `src/runtime/`.

## Components & responsibilities

### Front end (BASIC)

The BASIC front end performs tokenization, parsing, semantic analysis, and lowering to IL. Tokens are produced by a
hand-written lexer, and the recursive-descent parser builds an AST with nodes for statements and expressions.

Subcomponents and duties:

- **Lexer:** emits identifiers, numbers, strings, and keywords.
- **Parser:** produces the AST for BASIC constructs (e.g., `LET`, `PRINT`, `IF/THEN/ELSE`, `WHILE/WEND`, `GOTO`,
  `GOSUB/RETURN`).
- **Semantic analysis:** manages symbol tables, resolves types (`INT` vs `STRING`), enforces lvalue rules, performs
  simple constant folding, and handles suffix conventions such as `$` for strings.
- **Desugaring:** normalizes constructs (e.g., `ELSEIF` into nested `IF` blocks).
- **Lowering:** walks the AST and emits IL instructions via the IR builder.

Intrinsic functions like `LEN`, `LEFT$`, and `MID$` are looked up in a static registry during semantic analysis. They
lower to runtime calls such as `rt_str_len` and `rt_str_substr`.

Diagnostics flow through `DiagnosticEmitter`, which tracks file and line information. Errors stop compilation before
lowering, keeping the emitted IL well-formed.

### Intermediate Language (IL)

The IL is a typed, block-structured representation with SSA-like virtual registers. Functions contain labelled basic
blocks ending in explicit terminators, and the verifier checks single terminators per block, operand types, and call
signatures.

```text
+-------------------------------+
| IL Module                     |
|  - externs                    |
|  - globals                    |
|  - functions                  |
|     - blocks (phi/ops/term)   |
+-------------------------------+
```

Example:

```llvm
il 0.1
func @main() -> i64 {
entry:
  %v0 = add 2, 2
  ret %v0
}
```

Key design points:

- **Types:** `i1`, `i32`, `i64`, `f64`, `ptr`, `str`; keep the set minimal and orthogonal.
- **Values:** virtual registers, constants, globals, function symbols; names are interned for determinism.
- **Instructions (v1 focus):** arithmetic (`add`, `sub`, `mul`, `div`), bitwise ops, comparisons, control flow (`br`,
  `cbr`, `ret`, `trap`), memory ops (`alloca`, `load`, `store`, `gep`), calls, constant constructors, and minimal casts.
- **Metadata:** source locations, attributes, visibility, and string tables stored per module.
- **Calling convention:** by-value scalars with explicit pointers for aggregates; strings remain opaque handles
  manipulated through runtime helpers.

### Pass pipeline

`src/il/transform/PassManager` orchestrates the optimization pipeline. Available passes include:

- **CheckOpt** – validates optimizer-specific invariants.
- **ConstFold** – folds constant expressions.
- **DCE** – removes unreachable code and unused values.
- **DSE** – dead store elimination.
- **EarlyCSE** – early common subexpression elimination.
- **GVN** – global value numbering.
- **IndVarSimplify** – induction variable simplification.
- **Inline** – function inlining.
- **LateCleanup** – post-optimization cleanup pass.
- **LICM** – loop-invariant code motion.
- **LoopSimplify** – loop normalization.
- **LoopUnroll** – loop unrolling.
- **Mem2Reg** – memory-to-register promotion.
- **Peephole** – rewrites short instruction sequences.
- **SCCP** – sparse conditional constant propagation.
- **SimplifyCFG** – control flow graph simplification.

The verifier runs after passes to enforce correctness before execution or code generation.

### Runtime & ABI (externs)

Extern symbols in IL map to C functions declared in `src/runtime/rt.hpp`. Strings use reference-counted heap objects;
numeric values are 64-bit.

Initial runtime surface (all prefixed `rt_`):

- Console: `rt_input_line`, `rt_print_f64`, `rt_print_i64`, `rt_print_str`.
- Math helpers: `rt_cos`, `rt_pow_f64_chkdom`, `rt_sin`, etc.
- Memory: `rt_alloc` (reference-counted allocation; memory is freed automatically via retain/release).
- Strings: `rt_f64_to_str`, `rt_int_to_str`, `rt_str_concat`, `rt_str_len`, `rt_str_substr`, `rt_to_int`.

#### Runtime memory model

Strings and arrays share a single heap layout described by [`rt_heap.h`](../../src/runtime/rt_heap.h). Every payload
pointer is preceded by an `rt_heap_hdr_t` header containing a magic tag, the allocation kind, the element kind,
reference count, length, and capacity. The helper accessors in `rt_heap.c` validate this header on every retain/release
in debug builds, ensuring both strings and arrays obey the same invariants.

Arrays are true reference types: assigning to another variable or passing as a parameter forwards the handle and bumps
the refcount. No eager copy happens. When `rt_arr_i32_resize` observes a shared array (`refcnt > 1`) it allocates a
fresh payload, copies the active prefix, releases the old handle, and returns the new pointer—effectively
copy-on-resize. In-place growth only occurs when the array is uniquely owned.

Ownership rules mirror strings. The lowering pipeline emits retains on assignment boundaries, scope exit releases,
parameter teardown releases, and function returns transfer ownership to the caller. Violating these rules (e.g.,
releasing then reusing a temp in the same block) is caught by the IL verifier.

Define `VIPER_RC_DEBUG=1` (set automatically for Debug builds) or export the environment variable `VIPER_RC_DEBUG` to
make the runtime log every retain/release along with the resulting refcount. These checks highlight double releases,
missing retains, or stale handles early in development.

Additional runtime details live in the [Runtime & VM Guide](runtime-vm.md#runtime-memory-model).

Front-end intrinsics lower directly to these routines. Both the VM and native code call the same C ABI. The ABI is
versioned and may evolve; breaking changes require coordinated updates.

### VM interpreter

The VM is a register-file interpreter that dispatches opcodes using a pluggable strategy (function-table, switch, or
threaded/computed-goto). Each call creates a frame holding registers, an evaluation stack, and block state. Values are
stored in a tagged `Slot` that represents integers, floats, pointers, and strings.

Execution model and state:

- **Frame:** local virtual-register array, stack slots for `alloca`, and an instruction pointer (block + index).
- **Heap:** managed by the runtime for strings/arrays.
- **Call stack:** vector of frames, one per IL invocation.

Core dispatch sketch:

```cpp
for (;;) {
  switch (instr.opcode) {
    case OP_ADD:
      regs[d] = regs[a] + regs[b];
      ++ip;
      break;
    case OP_CBR:
      ip = regs[cond] ? then_bb->first : else_bb->first;
      break;
    case OP_CALL:
      regs[dst] = call_runtime_or_fn(fsym, args...);
      ++ip;
      break;
    case OP_RET:
      return regs[retv];
  }
}
```

Runtime services manage heap-allocated strings with reference counting. Extern calls bridge to C helpers for I/O and
string manipulation. The VM can trace execution (`--trace`, `--trace-calls`) to dump executed instructions, call/return
events, and value states, aiding debugging and performance analysis. Traps surface structured diagnostics that include
function, block, and source location information.

### Code generation

`src/codegen/` contains native backends for x86-64 and ARM64:

- **x86-64** (`src/codegen/x86_64/`) — Implemented backend targeting System V AMD64 and Windows x64 ABIs (linear-scan).
  Validated on Windows with full codegen test suite passing.
- **ARM64** (`src/codegen/aarch64/`) — Functional backend targeting AAPCS64 (Apple Silicon, Linux ARM64). Validated
  end‑to‑end on Apple Silicon by running a full Frogger demo.

Pipeline expectations:

1. **Lowering:** optionally translate IL to a simpler MIR or operate directly.
2. **Liveness:** compute live intervals for virtual registers.
3. **Register allocation:** linear scan with spill slots on the stack.
4. **Instruction selection:** greedy mapping (e.g., `add i64` → `addq`, comparisons → `cmp` + conditional branches).
5. **Prologue/Epilogue:** establish stack frame, preserve callee-saved registers, align stack.
6. **Calling convention:** map IL calls to SysV (GP args in `rdi`..`r9`, FP in `xmm0`..).
7. **Assembly emission:** generate `.s`, assemble to `.o`, and link with `librt.a`.
8. **Debug info (optional):** comments or DWARF metadata later.

Differential testing against the VM keeps codegen honest once implemented.

### Tools & CLI

The CLI (`viper`) dispatches to focused handlers based on the first tokens:

- `-run <file.il> [--trace] [--stdin-from <file>] [--max-steps N] [--bounds-checks]`
- `front basic -emit-il <file.bas> [--bounds-checks]`
- `front basic -run <file.bas> [--trace] [--stdin-from <file>] [--max-steps N] [--bounds-checks]`
- `il-opt <in.il> -o <out.il> --passes p1,p2`

Handlers live in `src/tools/viper/cmd_run_il.cpp`, `cmd_front_basic.cpp`, and `cmd_il_opt.cpp`; `src/tools/viper/main.cpp`
merely dispatches to these subcommands. Additional tools (verifier, disassembler) reuse the same IL libraries.

Diagnostics carry source mapping (file/line/column) through AST → IL → VM/native for clear errors, and a REPL (
`viper repl`) is a nice-to-have backed by the VM.

### Extensibility points

- **New front end:** add a directory under `src/frontends/` and emit IL modules that honor the runtime ABI.
- **New intrinsic:** register in `src/frontends/basic/Intrinsics.cpp`, implement a runtime extern, and extend the
  verifier if new types are involved.
- **New IL pass:** implement in `src/il/transform/`, register with `PassManager`, and ensure verifier invariants hold.
- **Additional languages:** reuse symbol-table and type-checker utilities, desugar loops into blocks/branches, and keep
  the IL small and orthogonal so backends remain simple.
- **Runtime growth:** extend the C ABI in backward-compatible ways; both the VM and native backends immediately benefit.

### Deterministic naming

Deterministic label naming ensures recompiling the same source yields identical IL. Deterministic labels keep golden
tests from drifting and make builds reproducible, so the IR builder interns symbols and assigns names deterministically.

### Performance notes

Interpreter hot spots include opcode dispatch and string routines. Constant folding and dead code elimination have the
largest impact on throughput. Current optimizations and future improvements:

- Threaded/computed-goto dispatch is already implemented and selected by default when supported.
- String literals are pre-cached during VM construction to eliminate repeated allocation.
- Frame buffers (register file and operand stack) are pooled across function calls.
- Future: further peephole rewrites, constant folding during IL build, and profile-guided dispatch selection.

### Compatibility & versioning

Modules declare an IL version (current: `il 0.2.0`) at the top. The runtime ABI is versioned; breaking changes require
bumping the IL version and updating consumers.

### Glossary

- **AST:** tree form produced by the parser.
- **IL:** intermediate language consumed by passes and backends.
- **Block:** sequence of instructions ending in a terminator.
- **Terminator:** instruction that ends a block (`ret`, `br`, `cbr`, `trap`).
- **Extern:** IL symbol resolved to a runtime C function.
- **Verifier:** checker enforcing IL invariants.
- **Pass:** transformation over an IL module.
- **VM frame:** stack record for a function invocation.

<a id="cpp-overview"></a>

## C++ Project Overview

### Guiding principles

- Single thin-waist IL shared by all front ends and back ends.
- Strong module boundaries: each layer builds its own library without cross-layer reach-through.
- Determinism first: the VM is the semantic oracle; native code must match it.
- Testable units: verifier, serializer, VM, and backend maintain golden/e2e coverage.
- Solo-friendly: minimal dependencies, predictable build, incremental milestones.

### Repository layout (CMake, modular)

```
/CMakeLists.txt
/cmake/              # compiler flags, toolchain helpers
/docs/               # IL spec, developer docs, ADRs
/scripts/            # dev scripts (format, lint, build, test)
/src/
  buildmeta/         # version files (IL_VERSION, VERSION)
  bytecode/          # bytecode compiler, module format, VM
  codegen/           # native backends (aarch64, common, x86_64)
  common/            # cross-cutting utilities (mangling, integer helpers, process runner)
  frontends/         # language front ends (basic/, common/, zia/)
  il/                # core types, IR, builder, verifier, analysis, transforms
  lib/graphics/      # ViperGFX 2D graphics library
  parse/             # cursor utilities
  pass/              # generic pass manager facade
  runtime/           # C runtime (libviper_runtime.a): rt_*.c, rt.h
  support/           # shared utilities (diagnostics, arena, source manager)
  tests/             # unit/, golden/, e2e/, smoke/, perf/ and more
  tools/             # CLI tools (viper/, vbasic/, ilrun/, zia/, etc.)
  tui/               # terminal UI library
  vm/                # interpreter
```

Top-level CMake targets include `il_core`, `il_vm`, `il_codegen_x86_64`, `il_codegen_aarch64`, `frontend_basic`,
`librt`, CLI executables (`viper`, `vbasic`, `ilrun`, `il-dis`, `il-verify`), and dedicated test binaries.

### Tooling & Build

- C++20 (`-std=c++20`) with `-Wall -Wextra -Wpedantic -Werror`.
- Sanitizers (`-fsanitize=address,undefined`) in dev/CI.
- Dependencies kept small and vendorable: `fmt`, CLI11/lyra, and a tiny in-tree test harness.
- Formatting via `clang-format`, linting with `clang-tidy`.
- CI matrix (Linux/macOS) caches builds and runs sanitizers plus tests.

### Namespaces and libraries

- `il::core`, `il::build`, `il::io`, `il::verify` for IL infrastructure.
- `il::vm` for the interpreter engine.
- `il::codegen::x86_64` for the native backend.
- `fe::basic` for the BASIC front end.
- `rt` (C ABI) for the runtime library.

### Core IL library (C++)

#### Data model (lightweight, cache-friendly)

- `TypeKind` enum (`Void`, `I1`, `I64`, `F64`, `Ptr`, `Str`) with lightweight equality/hash.
- `Value` tagged structs (`Temp`, `ConstInt`, `ConstFloat`, `ConstStr`, `GlobalAddr`) with compact payloads.
- `Instr` stores opcode, result type, optional destination, operands (`small_vector`), and `SourceLoc`.
- IR aggregates: `BasicBlock`, `Function` (params, return type, blocks, attributes, visibility), `Global`, and
  `Module` (target triple, extern declarations, string literals).
- Symbol tables use interned strings to avoid duplication.

#### IRBuilder (ergonomic construction)

- Tracks current function and block to ensure well-formedness.
- Provides helpers like `const_i64`, `add`, `br`, `cbr`, and `call`.
- Guarantees exactly one terminator per block and records source metadata during lowering.

#### IL I/O (text format)

- Deterministic serializer that sorts externs/globals and assigns consistent temporary numbers.
- Parser implemented with a small tokenizer and recursive-descent grammar tailored to the IL spec.
- Round-trip tests (`parse → print → parse`) ensure structural equivalence.

#### Verifier

- Structural checks: one terminator per block, labels defined/used, dominance of operands.
- Type checks: operands match opcode expectations; calls respect arity and signatures.
- Alignment rules for memory operations.
- Deliverable: `il_verify(Module&) -> std::vector<Diagnostic>`.

### Runtime library (/runtime, C ABI; evolving)

The runtime provides a comprehensive C ABI with the following components:

**Core modules:**

- **Arrays**: `rt_array.h`, `rt_array.c` - dynamic arrays with copy-on-resize semantics
- **Errors**: `rt_error.h`, `rt_error.c`, `rt_trap.h`, `rt_trap.c` - trap and error handling
- **Files**: `rt_file.h`, `rt_file.c`, `rt_file_io.c` - file operations (open, read, write, seek)
- **I/O**: `rt_io.c` - console printing and line input
- **Math**: `rt_fp.c`, `rt_math.c` - mathematical functions, floating-point utilities
- **Memory**: `rt_heap.h`, `rt_heap.c`, `rt_memory.c` - allocation, reference counting, heap management
- **Numerics**: `rt_format.c`, `rt_int_format.c`, `rt_numeric.c` - deterministic numeric conversions
- **OOP**: `rt_oop.h`, `rt_oop_dispatch.c`, `rt_type_registry.c` - object system (vtables, method dispatch)
- **Strings**: `rt_string.h`, `rt_string_builder.c`, `rt_string_encode.c`, `rt_string_format.c`, `rt_string_ops.c` -
  string operations, conversion, formatting
- **Terminal**: `rt_term.c` - ANSI terminal control (CLS, COLOR, LOCATE, cursor visibility)
- **Time/Random**: `rt_random.h`, `rt_random.c`, `rt_time.c` - TIMER, RNG with seeding

**Headers**: `rt.hpp` (VM bridge) and individual `.h` files declare the C ABI.
**String representation**: ref-counted heap blocks (magic tag + refcount + length + capacity + UTF-8 bytes).
**Build**: Compiles to static library `libviper_runtime.a`, linked by both VM host and native codegen outputs.

### Interpreter (`il::vm`)

#### Execution engine

- Types: `Slot` (tagged union of `uint64_t`, `double`, pointers), `Frame` (function reference, register array, stack for
  `alloca`, instruction cursor), and `VM` (module pointer, host function table, call stack).
- Dispatch uses a pluggable strategy: function-table (default portable), switch (inline handlers), or threaded/computed-goto (GCC/Clang, fastest).
- `alloca` implemented as a bump pointer in the frame-local stack; memory ops rely on `memcpy` with runtime checks.
- Calls push new frames for IL functions or marshal arguments to C externs.

#### Tracing & diagnostics

- Flags like `--trace` and `--trace-calls` expose executed instructions and frame transitions.
- Traps propagate structured errors with location metadata, enabling pretty diagnostics at the CLI layer.

### Codegen

#### x86-64 (`viper::codegen::x64`)

Implemented pipeline (validated on Windows with full test suite passing):

1. Lower IL to MIR (Machine IR with virtual registers).
2. Instruction selection and legalization.
3. Linear-scan register allocation with spilling.
4. Frame layout (callee-saved registers, spill area, outgoing args).
5. Assembly emission (AT&T syntax, GAS-compatible).

#### ARM64 (`viper::codegen::aarch64`)

Functional and validated end‑to‑end on Apple Silicon. Pipeline:

1. Lower IL to AArch64 MIR.
2. Linear-scan register allocation.
3. Frame construction (FP-relative addressing).
4. Assembly emission (ARM GAS syntax).

## Archived blueprint highlights

### Testing strategy (solo-friendly)

- Golden tests: source → expected IL text.
- VM end-to-end tests: run IL on the interpreter, assert stdout/return codes.
- Backend end-to-end tests: compile to native and compare outputs to VM results.
- Differential testing: VM versus native outputs for each sample.
- Verifier unit tests: malformed IR cases to ensure detection.
- Lightweight fuzzing: stress lexer/parser with small random inputs.

### Extensibility for new languages

- Contract: new front ends must emit valid IL and adhere to the runtime ABI.
- BASIC specifics: keywords map to straightforward control flow and runtime calls; dynamic-leaning typing handled via
  coercions to the IL's small type set (`i64`, `f64`, `str`) plus runtime helpers.
- Additional front ends: reuse symbol-table utilities, keep desugaring consistent, and
  avoid pushing complexity into the IL.

### IL details worth nailing early

- Prefer `i64` and `f64` as canonical numeric types; use `i1` for booleans and keep strings opaque.
- Clearly define undefined behavior (e.g., division by zero triggers a runtime diagnostic).
- Verifier must enforce operand dominance, single terminators per block, type correctness, call signature adherence, and
  no implicit fallthrough.

### Interpreter vs. codegen division of labor

- Interpreter acts as the semantic oracle and fastest path to functionality.
- Backend focuses on performance and binary output, never redefining language behavior.
- When native execution diverges, compare to VM output to isolate codegen defects.

### Minimal code sketches

```cpp
// IR builder usage
Value v1 = b.const_i64(2);
Value v2 = b.const_i64(3);
Value sum = b.add(v1, v2);
b.call(sym("rt_print_i64"), {sum});
b.ret(b.const_i32(0));
```

```c
// Interpreter dispatch sketch
for (;;) {
    Instr *i = ip++;
    switch (i->op) {
        case OP_ADD:
            regs[i->dst] = regs[i->a].i64 + regs[i->b].i64;
            break;
        case OP_CBR:
            ip = regs[i->cond].i1 ? i->tgt : i->ftgt;
            break;
        case OP_CALL:
            regs[i->dst] = call_host(i->callee, regs, i->argc);
            break;
        case OP_RET:
            return regs[i->retv];
    }
}
```

```asm
# Assembly emission sketch (x86-64 SysV)
push %rbp
mov %rsp, %rbp
sub $32, %rsp      # spill area
# ... instructions mapped from IL ...
mov %rbp, %rsp
pop %rbp
ret
```

### Diagnostics & developer UX

- Carry source mapping through all stages for precise errors.
- Show source snippets with carets when reporting diagnostics.
- Optional VM tracing flag (`--trace-il`) prints executed IL instructions with values.
- A REPL built on the VM would improve interactive workflows.

### Performance considerations

- Interpreter: investigate computed gotos for dispatch, intern common strings, and cache constant values.
- Backend: add peephole optimizations, leverage constant folding during IL construction, and improve register
  allocation.

### Risks & mitigations

- Scope creep → enforce a strict v1 feature set and milestone-based roadmap.
- Type-system complexity → keep IL types minimal and push conversions to front ends/runtime helpers.
- String/heap bugs → start with ref-counted strings, add ASAN/UBSAN in CI, and build a focused test suite.
- Codegen pitfalls → lean on VM-oracle differential tests and begin with a single platform/ABI.

### Suggested roadmap

- **Milestone A (bring-up):** BASIC front end for `PRINT`, `LET`, `IF`, `GOTO`; IL core + verifier + serializer; runtime
  printing; VM executes small programs with golden tests.
- **Milestone B (control & funcs):** `WHILE/WEND`, `FOR/NEXT`, simple functions; runtime input (`rt_input_line`); CLI
  flags `-emit-il`, `-run`.
- **Milestone C (codegen v1):** x86-64 SysV codegen, linear-scan register allocation, emit `.s`, assemble/link with
  `librt.a`; differential tests (VM vs native).
- **Milestone D (quality):** peephole optimizer, improved diagnostics, expanded library (strings/math/file I/O);
  optional bytecode encoding for a faster VM.

### Definition of done for v1

- BASIC subset compiles to IL, runs correctly on the VM, and compiles to native with matching outputs.
- Clear CLI, docs, and tests accompany the implementation.
- Clean separation between front end ↔ IL ↔ VM/codegen ↔ runtime.
- Interpreter-first workflow keeps the IL as the versioned contract between language and machine layers.

<a id="tui-architecture"></a>

## ViperTUI Architecture

ViperTUI is an experimental terminal UI library built in layers. Each layer stays focused and exposes a small surface so
higher tiers can be tested without a real terminal.

### Layers

#### Term

Low-level terminal handling lives under `src/tui/src/term/`. `TermIO` abstracts writes to the terminal while
`TerminalSession` configures raw mode and manages alt-screen state. Clipboard support uses OSC 52 sequences but can be
disabled for tests.

#### Render

`src/tui/src/render/` converts a widget tree into escape sequences. It maintains an in-memory surface and computes
minimal diffs before emitting to `TermIO`.

#### UI

`src/tui/src/ui/` holds the widget tree and focus management. It delivers input events, invokes widget callbacks, and
triggers re-renders when state changes.

#### Widgets

Reusable components such as lists, containers, and modals live in `src/tui/src/widgets/`. Widgets compose other widgets
and render through the UI and render layers.

#### Text

`src/tui/src/text/` provides buffer management and search utilities used by widgets that edit or display text.

#### Tests

Tests exercise the layers without a real TTY by using `StringTermIO` to capture rendered output. Setting
`VIPERTUI_NO_TTY=1` ensures `TerminalSession` stays inactive so tests run headless.

### Environment Flags

- `VIPERTUI_NO_TTY` – when set to `1`, `TerminalSession` skips TTY setup and the application renders a single frame then
  exits (useful for CI and tests).
- `VIPERTUI_DISABLE_OSC52` – disables OSC 52 clipboard sequences so tests do not emit control codes on unsupported
  terminals.

### Headless Testing Pattern

```cpp
using tui::term::StringTermIO;

// Force headless mode and capture output.
setenv("VIPERTUI_NO_TTY", "1", 1);
StringTermIO tio;           // acts like a fake terminal
// ... render widgets using `tio` ...
assert(tio.buffer().find("expected text") != std::string::npos);
```

`StringTermIO` records all writes and allows assertions on the exact escape sequences produced by the render layer.

Sources: docs/architecture.md; docs/architecture.md#tui-architecture; archive/docs/architecture.md#cpp-overview;
archive/docs/project-overview.md; archive/docs/dev-cli.md; archive/docs/dev/architecture.md.
