---
status: stable
audience: contributor
last-verified: 2025-09-11
---
# Viper Architecture Overview

**Purpose:** This document explains how Viper compiles and runs programs end-to-end:
front ends (e.g., BASIC) → intermediate language (IL) → optimization passes →
execution on the VM interpreter → (future) native code generation. It is a map
to the system; deep dives live in linked pages.

If you're new to the IL, start with the [IL Quickstart](il-quickstart.md).

## High-level pipeline

The core stages and artifacts:

- **Front end (BASIC):** lex/parse → AST → semantic analysis (types, arity, lvalues)
- **Lowering:** AST → IL module (functions, blocks, instructions)
- **Optimization passes:** constant folding, dead code elimination, peephole
- **Execution backends:**
  - **VM interpreter** (primary today)
  - **Code generation** (experimental, see below)

```text
BASIC source
   │  tokenize / parse
   ▼
 AST ── semantic checks ── IL builder ── passes ──► VM interpreter
                                   │
                                   └───────────────► native codegen (future)
```

See also: [BASIC reference](reference/basic-language.md),
[IL Quickstart](il-quickstart.md), and
[IL reference](references/il.md).

## Source layout (where things live)

- **Front end:** `src/frontends/basic/`
- **IL core:** `src/il/core/`, `src/il/io/`, `src/il/build/`, `src/il/verify/`
- **Passes:** `src/il/transform/`
- **VM:** `src/vm/`, `runtime/`
- **Code generation:** `src/codegen/`
- **Tools:** `src/tools/ilc/`
- **Docs & examples:** `docs/`, `examples/`

## Front end (BASIC)

The BASIC front end performs tokenization, parsing, semantic analysis, and
lowering to IL. Tokens are produced by a hand-written lexer. The recursive-descent
parser builds an AST with nodes for statements and expressions. The semantic
phase resolves types (`INT` vs `STRING`), ensures lvalue correctness, and
handles suffix conventions such as `$` for strings.

Intrinsic functions like `LEN`, `LEFT$`, and `MID$` are looked up in a static
registry during semantic analysis. They lower to runtime calls such as
`rt_len` and `rt_substr`.

Diagnostics are reported through `DiagnosticEmitter`, which tracks file and line
information. Errors stop compilation before lowering.

```basic
10 LET S$ = "HELLO"
20 PRINT LEFT$(S$, 2)
30 END
```

For language details see the [BASIC reference](reference/basic-language.md).

## IL (Intermediate Language)

The IL is a typed, block-structured representation. Functions contain labelled
basic blocks ending in explicit terminators. Values are in SSA-like virtual
registers. The verifier checks single terminators per block, operand types, and
call signatures.

```text
+-------------------------------+
| IL Module                     |
|  - globals                    |
|  - functions                  |
|     - blocks (phi/ops/term)   |
+-------------------------------+
```

```il
il 0.1.2
fn @main() -> i64 {
entry:
  %v0 = add 2, 2
  ret %v0
}
```

More syntax and semantics are covered in the [IL reference](references/il.md).

## Pass pipeline

`src/il/transform/PassManager` orchestrates the optimization pipeline. Passes run
in a fixed order:

1. **ConstFold** – folds constant expressions.
2. **Peephole** – rewrites short instruction sequences.
3. **DCE** – removes unreachable code and unused values.

The verifier runs after passes to enforce correctness before execution or code
generation.

## VM interpreter

The VM is a stack machine that dispatches opcodes in a `switch` loop. Each call
creates a frame holding registers, an evaluation stack, and block state. Values
are stored in a tagged `Slot` that represents integers, floats, pointers, and
strings.

Runtime services manage heap-allocated strings with reference counting. Extern
calls bridge to C helpers for I/O and string manipulation. The VM can trace
execution steps for debugging and performance analysis.

```text
           +-------------------+
           |   VM Interpreter  |
           +-------------------+
    call → |  call frames      | ← ret
           |  eval stack       |
           |  heap (strings)   |
           +-------------------+
               ▲         ▲
               │         │
             IL ops   runtime/rt.*
```

## Runtime & ABI (externs)

Extern symbols in IL map to C functions declared in `runtime/rt.hpp`. Strings use
reference-counted heap objects; numeric values are 64-bit. Typical externs
include `rt_len` (string length), `rt_concat` (concatenate), and `rt_substr`
(substring). Front-end intrinsics lower directly to these routines.

## Code generation (current state and roadmap)

`src/codegen/x86_64/` contains an experimental backend stub. It does not yet
produce runnable machine code. The long-term plan is to translate IL modules to
SysV x86-64, reusing existing passes for optimization. Until then, the VM is the
primary execution engine.

## End-to-end lifecycle (what happens when you run a program)

`ilc` is the command-line entry point. It parses arguments, loads source files,
and drives the compile and execute pipeline:

1. BASIC front end emits IL.
2. PassManager applies optimizations.
3. Verifier checks invariants.
4. VM loads the module and runs `main`.

```sh
$ ilc run examples/basic/ex1_hello_cond.bas
HELLO
READY
10
10
```

## Extensibility points

- **New front end:** add a directory under `src/frontends/` and emit IL modules.
- **New intrinsic:** register in `src/frontends/basic/Intrinsics.cpp`, implement a
  runtime extern, and extend the verifier if new types are involved.
- **New IL pass:** implement in `src/il/transform/`, register with `PassManager`,
  and ensure verifier invariants hold.

## Performance notes

Interpreter hot spots include opcode dispatch and string routines. Constant
folding and dead code elimination have the largest impact on throughput. Enable
tracing in the VM to profile execution and confirm pass effects.

## Compatibility & versioning

Modules declare an IL version (`il 0.1.2`) at the top. The runtime ABI aims to
remain stable across versions; breaking changes require bumping the IL version
and updating consumers.

## Glossary

- **AST:** tree form produced by the parser.
- **IL:** intermediate language consumed by passes and backends.
- **Block:** sequence of instructions ending in a terminator.
- **Terminator:** instruction that ends a block (`ret`, `br`, `cbr`, `trap`).
- **Extern:** IL symbol resolved to a runtime C function.
- **Verifier:** checker enforcing IL invariants.
- **Pass:** transformation over an IL module.
- **VM frame:** stack record for a function invocation.

## References

- [BASIC reference](reference/basic-language.md)
- [IL reference](references/il.md)

