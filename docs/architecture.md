---
status: active
audience: developers
last-verified: 2026-02-09
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

- IL Core/IO/Verify/Analysis/Transform — Intermediate Language data structures, parsers/serializers, verifier, analysis
  passes, and transforms
- VM — Stack-based interpreter for IL with tracing, debugging, and runtime bridge
- Frontends — BASIC and Zia front ends lowering to IL
- Codegen — Native code generation backends (AArch64 validated; x86_64 validated on Windows)
- Tools — CLI entry points (viper, vbasic, zia, ilrun, il-verify, il-dis, BASIC/Zia helpers)
- Runtime — C runtime library and ABI bridge used by IL and VM (includes cycle-detecting GC, unified serialization,
  concurrent collections, 2D physics, async combinators, and full ViperDOS platform support)

---

## Source Layout

- src/il/core — IR types, opcodes, modules, functions, values
- src/il/io — Text parser/serializer for `.il` files
- src/il/verify — IL verifier (operand types, counts, control flow, EH)
- src/il/analysis — IL analyses (CFG, Dominators, BasicAA)
- src/il/transform — Pass framework and built-in passes (DCE, SCCP, LICM, SimplifyCFG, Mem2Reg)
- src/vm — VM core (`VM.hpp/.cpp`), opcode handlers, debug/tracing, runtime bridge
- src/codegen/x86_64 — X64 lowering, machine IR, register allocation pipeline
- src/codegen/aarch64 — ARM64 lowering, machine IR, linear-scan register allocation
- src/codegen/common — Shared codegen utilities (label utilities, linker support, diagnostics, target info)
- src/bytecode — Bytecode compiler, module format, and bytecode VM
- src/frontends/basic — BASIC frontend (lexer, parser, semantic analysis, IL lowerer)
- src/frontends/zia — Zia frontend (lexer, parser, semantic analysis, IL lowerer)
- src/frontends/common — Shared frontend utilities
- src/tools/viper — Unified compiler driver (run IL, front basic, il-opt, codegen)
- src/tools/zia — Zia compiler driver
- src/tools/vbasic — Friendly BASIC wrapper over `viper front basic`
- src/tools/ilrun — Friendly IL runner wrapper over `viper -run`
- src/tools/il-verify — Standalone IL verifier CLI
- src/tools/il-dis — IL disassembler (text pretty-printer)
- src/tools/basic-ast-dump, src/tools/basic-lex-dump — BASIC developer utilities
- src/runtime — C runtime and bridges used by the VM and IL externs (380+ files: memory/GC, strings, collections,
  math, I/O, graphics, audio, input, networking, threading, text, time, crypto, serialization, physics, async)
- src/support — Shared utilities (diagnostics, arena, source manager, symbols, alignment)
- src/common — Common infrastructure shared across subsystems
- src/parse — Parsing utilities (Cursor)
- src/pass — Pass manager infrastructure
- src/lib/graphics — ViperGFX 2D graphics library
- src/tui — Terminal UI utilities and components

Public headers are under `include/viper/...`:

- include/viper/il — Public IL APIs (Module, IRBuilder, Verify, IO)
- include/viper/vm — Public VM surface (VM, RuntimeBridge, OpcodeNames, Debug)
- include/viper/runtime — C runtime headers (`rt.h`, `rt_oop.h`)
- include/viper/diag — Diagnostic API (`BasicDiag.hpp`)

---

## CLI Tools (Built Targets)

- `viper` — Unified driver
    - `-run <file.il>` — Execute IL on the VM
    - `front basic -emit-il|-run <file.bas>` — BASIC compile/run
    - `il-opt <in.il> -o <out.il> [--passes ...]` — Optimizer
    - `codegen x64 -S <in.il> [-o exe] [--run-native]` — x86-64 native path
    - `codegen arm64 <in.il> -S <out.s>` — AArch64 assembly generation
- `vbasic` — Convenience wrapper for BASIC (`vbasic script.bas --emit-il|-o`)
- `zia` — Zia compiler (`zia script.zia` to run, `zia script.zia --emit-il` to emit IL)
- `ilrun` — Convenience wrapper for IL execution (`ilrun program.il`)
- `il-verify` — IL structural/type verifier
- `il-dis` — IL disassembler / pretty-printer

---

## Notes

- IL version: see `src/buildmeta/IL_VERSION` (current: 0.2.0)
- Keep layering strict: frontends do not depend on VM/codegen; VM does not include codegen; codegen depends only on IL
  core/verify/support
- See also: `devdocs/architecture.md` for a deeper architectural discussion
