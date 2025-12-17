---
status: active
audience: developers
last-verified: 2025-11-18
---

# C++ Project Overview

This document outlines the current C++ layout of the Viper compiler stack. It reflects the live source tree and should
be treated as the source of truth for directory roles and public entry points.

Status notes:

- VM is the primary execution target used for development and testing.
- Native backends are experimental: AArch64 has been validated by running a full Frogger demo on Apple Silicon; x86_64
  is implemented but has not yet been tested on real x86 hardware.

---

## High-Level Layers

- IL Core/IO/Verify/Analysis/Transform — Intermediate Language data structures, parsers/serializers, verifier, analysis
  passes, and transforms
- VM — Stack-based interpreter for IL with tracing, debugging, and runtime bridge
- Frontends — BASIC and Pascal front ends lowering to IL
- Codegen — Native code generation backends (AArch64 validated; x86_64 experimental)
- Tools — CLI entry points (ilc, vbasic, vpascal, ilrun, il-verify, il-dis, BASIC/Pascal helpers)
- Runtime — C runtime library and ABI bridge used by IL and VM

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
- src/codegen/common — Shared codegen utilities (argument normalization, label utilities)
- src/tools/ilc — Unified compiler driver (run IL, front basic, il-opt, codegen)
- src/tools/vbasic — Friendly BASIC wrapper over `ilc front basic`
- src/tools/ilrun — Friendly IL runner wrapper over `ilc -run`
- src/tools/il-verify — Standalone IL verifier CLI
- src/tools/il-dis — IL disassembler (text pretty-printer)
- src/tools/basic-ast-dump, src/tools/basic-lex-dump — BASIC developer utilities
- src/runtime — C runtime and bridges used by the VM and IL externs

Public headers are under `include/viper/...`:

- include/viper/il — Public IL APIs (Module, IRBuilder, Verify, IO)
- include/viper/vm — Public VM surface (VM, OpcodeNames, Debug)
- include/viper/runtime — C runtime headers (`rt.h`, `rt_oop.h`)

---

## CLI Tools (Built Targets)

- `ilc` — Unified driver
    - `-run <file.il>` — Execute IL on the VM
    - `front basic -emit-il|-run <file.bas>` — BASIC compile/run
    - `il-opt <in.il> -o <out.il> [--passes ...]` — Optimizer
    - `codegen x64 -S <in.il> [-o exe] [--run-native]` — x86-64 native path
    - `codegen arm64 <in.il> -S <out.s>` — AArch64 assembly generation
- `vbasic` — Convenience wrapper for BASIC (`vbasic script.bas --emit-il|-o`)
- `vpascal` — Convenience wrapper for Pascal (`vpascal program.pas --emit-il|-o`)
- `ilrun` — Convenience wrapper for IL execution (`ilrun program.il`)
- `il-verify` — IL structural/type verifier
- `il-dis` — IL disassembler / pretty-printer

---

## Notes

- IL version: see `src/buildmeta/IL_VERSION` (current: 0.1.2)
- Keep layering strict: frontends do not depend on VM/codegen; VM does not include codegen; codegen depends only on IL
  core/verify/support
- See also: `/devdocs/architecture.md` for a deeper architectural discussion
