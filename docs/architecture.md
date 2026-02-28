---
status: active
audience: developers
last-verified: 2026-02-17
---

# C++ Project Overview

This document outlines the current C++ layout of the Viper compiler stack. It reflects the live source tree and should
be treated as the source of truth for directory roles and public entry points.

Status notes:

- VM is the primary execution target used for development and testing.
- Native backends: AArch64 validated on Apple Silicon (full Frogger demo); x86_64 validated on Windows with
  full codegen test suite passing.

---

## High-Level Layers

- Codegen — Native code generation backends (AArch64 validated; x86_64 validated on Windows)
- Frontends — BASIC and Zia front ends lowering to IL
- IL Core/IO/Verify/Analysis/Transform — Intermediate Language data structures, parsers/serializers, verifier, analysis
  passes, and transforms
- Runtime — C runtime library and ABI bridge used by IL and VM (includes cycle-detecting GC, unified serialization,
  concurrent collections, 2D physics, async combinators, and full ViperDOS platform support)
- Tools — CLI entry points (viper, vbasic, zia, ilrun, il-verify, il-dis, BASIC/Zia helpers)
- VM — Register-file interpreter for IL with pluggable dispatch (function-table, switch, threaded), tracing, debugging,
  and runtime bridge

---

## Source Layout

- src/bytecode — Bytecode compiler, module format, and bytecode VM
- src/codegen/aarch64 — ARM64 lowering, machine IR, linear-scan register allocation
- src/codegen/common — Shared codegen utilities (label utilities, linker support, diagnostics, target info)
- src/codegen/x86_64 — X64 lowering, machine IR, register allocation pipeline
- src/common — Common infrastructure shared across subsystems
- src/frontends/basic — BASIC frontend (lexer, parser, semantic analysis, IL lowerer)
- src/frontends/common — Shared frontend utilities
- src/frontends/zia — Zia frontend (lexer, parser, semantic analysis, IL lowerer)
- src/il/analysis — IL analyses (BasicAA, CallGraph, CFG, Dominators)
- src/il/api — Public API facades (expected_api)
- src/il/build — IR builder (IRBuilder)
- src/il/core — IR types, opcodes, modules, functions, values
- src/il/internal — Internal IL I/O utilities
- src/il/io — Text parser/serializer for `.il` files
- src/il/link — Module linker for cross-language interop (merge, boolean thunks)
- src/il/runtime — Runtime signatures, helper effects, class name maps
- src/il/transform — Pass framework and built-in passes (CheckOpt, ConstFold, DCE, DSE, EarlyCSE, GVN, IndVarSimplify, Inline, LateCleanup, LICM, LoopSimplify, LoopUnroll, Mem2Reg, Peephole, SCCP, SimplifyCFG)
- src/il/utils — Shared IL utilities (UseDefInfo, Utils — use-def info and common helpers)
- src/il/verify — IL verifier (operand types, counts, control flow, EH)
- src/lib/graphics — ViperGFX 2D graphics library
- src/parse — Parsing utilities (Cursor)
- src/pass — Pass manager infrastructure
- src/runtime — C runtime and bridges used by the VM and IL externs (380+ files: memory/GC, strings, collections,
  math, I/O, graphics, audio, input, networking, threading, text, time, crypto, serialization, physics, async)
- src/support — Shared utilities (diagnostics, arena, source manager, symbols, alignment)
- src/tools/basic-ast-dump, src/tools/basic-lex-dump — BASIC developer utilities
- src/tools/il-dis — IL disassembler (text pretty-printer)
- src/tools/il-verify — Standalone IL verifier CLI
- src/tools/ilrun — Friendly IL runner wrapper over `viper -run`
- src/tools/rtgen — Runtime code generation helper
- src/tools/vbasic — Friendly BASIC wrapper over `viper front basic`
- src/tools/viper — Unified compiler driver (run IL, front basic, il-opt, codegen)
- src/tools/zia — Zia compiler driver
- src/tui — Terminal UI utilities and components
- src/vm — VM core (`VM.hpp/.cpp`), opcode handlers, debug/tracing, runtime bridge

Public headers are under `include/viper/...`:

- include/viper/diag — Diagnostic API (`BasicDiag.hpp`)
- include/viper/il — Public IL APIs (Module, IRBuilder, Verify, IO)
- include/viper/parse — Parsing utilities (`Cursor.h`)
- include/viper/pass — Pass manager (`PassManager.hpp`)
- include/viper/runtime — C runtime headers (`rt.h`, `rt_oop.h`)
- include/viper/vm — Public VM surface (VM, RuntimeBridge, OpcodeNames, Debug)

---

## CLI Tools (Built Targets)

- `il-dis` — IL disassembler / pretty-printer
- `il-verify` — IL structural/type verifier
- `ilrun` — Convenience wrapper for IL execution (`ilrun program.il`)
- `vbasic` — Convenience wrapper for BASIC (`vbasic script.bas --emit-il|-o`)
- `viper` — Unified driver
    - `-run <file.il>` — Execute IL on the VM
    - `codegen arm64 <in.il> -S <out.s> [-run-native]` — AArch64 assembly generation / native run
    - `codegen x64 -S <in.il> [-o exe] [-run-native]` — x86-64 native path
    - `front basic -emit-il|-run <file.bas>` — BASIC compile/run
    - `il-opt <in.il> -o <out.il> [--passes ...]` — Optimizer
- `zia` — Zia compiler (`zia script.zia` to run, `zia script.zia --emit-il` to emit IL)

---

## Notes

- IL version: see `src/buildmeta/IL_VERSION` (current: 0.2.0)
- Keep layering strict: frontends do not depend on VM/codegen; VM does not include codegen; codegen depends only on IL
  core/verify/support
- Cross-language interop: Zia and BASIC modules can be linked at the IL level. See [Cross-Language Interop
  Guide](interop.md) for syntax, type compatibility, and boolean bridging
- See also: `devdocs/architecture.md` for a deeper architectural discussion
