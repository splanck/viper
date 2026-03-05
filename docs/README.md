---
status: active
audience: public
last-verified: 2026-03-04
---

# Viper Documentation

Documentation for the Viper compiler toolchain: two language frontends (Zia and BASIC), a shared intermediate language (IL), a VM, native code generation backends, and a comprehensive runtime library.

## Start Here

| Document | Description |
|----------|-------------|
| [Getting Started](getting-started.md) | Build, install, and run your first program |
| [Zia Tutorial](zia-getting-started.md) | Learn Zia by example |
| [BASIC Tutorial](basic-language.md) | Learn Viper BASIC by example |
| [FAQ](faq.md) | Common questions answered |

---

## Languages

### Zia

- [Zia Tutorial](zia-getting-started.md) — Learn Zia with hands-on examples
- [Zia Reference](zia-reference.md) — Complete language specification
- [Feature Parity Matrix](feature-parity.md) — Zia vs BASIC feature comparison
- [Generics Plan](GENERICS_IMPLEMENTATION_PLAN.md) — Zia generics design (in progress)

### Viper BASIC

- [BASIC Tutorial](basic-language.md) — Learn Viper BASIC by example
- [BASIC Reference](basic-reference.md) — Complete language specification
- [Namespace Reference](basic-namespaces.md) — BASIC namespace system
- [Grammar Notes](basic-grammar.md) — BASIC grammar extensions
- [Lifetime Model](lifetime.md) — Reference counting and disposal

### Cross-Language

- [Cross-Language Interop](interop.md) — Zia + BASIC IL linking, type compatibility
- [Arithmetic Semantics](arithmetic-semantics.md) — Overflow, rounding, and numeric guarantees

---

## Intermediate Language (IL)

- [IL Guide](il-guide.md) — Comprehensive specification (normative)
- [IL Quickstart](il-quickstart.md) — Fast introduction for developers
- [IL Reference](il-reference.md) — Opcode and type reference
- [IL Passes](il-passes.md) — Optimization pass infrastructure

---

## Runtime Library

- [Runtime Library Index](viperlib.md) — All `Viper.*` classes and methods
- [Runtime API Reference](runtime-api-complete.md) — Complete runtime API
- [Runtime Extension How-To](runtime_extend_howto.md) — Add new classes and functions
- [Graphics Library](graphics-library.md) — ViperGFX 2D graphics API
- [Memory Management](memory-management.md) — GC, allocation, and object lifetime

---

## Tools & CLI

- [CLI Tools Reference](tools.md) — `viper`, `zia`, `vbasic`, `ilrun`, `il-verify`, `il-dis`
- [REPL](repl.md) — Interactive Zia/BASIC environment
- [Debugging Guide](debugging.md) — VM tracing, breakpoints, and diagnostics
- [Testing Guide](testing.md) — Unit, golden, e2e, and performance tests

---

## Internals

- [Architecture](architecture.md) — System design: frontends, IL, VM, codegen
- [Code Map](codemap.md) — Source directory layout and subsystem deep-dives
- [VM Guide](vm.md) — VM design, dispatch, profiling, and runtime ABI
- [Threading and Globals](threading-and-globals.md) — VM threading model
- [Backend Guide](backend.md) — x86-64 and ARM64 native code generation
- [Generated Files](generated-files.md) — Auto-generated C++ sources
- [Contributor Guide](contributor-guide.md) — Style guide and contribution process

---

## Specifications

- [specs/errors.md](specs/errors.md) — Trap kinds, handler semantics, error model
- [specs/numerics.md](specs/numerics.md) — Numeric types, ranges, IEEE semantics
- [abi/object-layout.md](abi/object-layout.md) — Object layout, vtable, call ABI
- [codegen/aarch64.md](codegen/aarch64.md) — AArch64 backend status
- [codegen/x86_64.md](codegen/x86_64.md) — x86-64 SysV ABI reference

---

## The Viper Bible

A comprehensive learning resource organized as a book.

- [Bible Index](bible/README.md) — Table of contents and chapter list
- [Writing Guide](bible/WRITING-GUIDE.md) — Style guide for Bible chapters
- [Content Inventory](bible/INVENTORY.md) — Chapter-to-source mapping

---

## Release Notes

- [v0.2.2](release_notes/Viper_Release_Notes_0_2_2.md)
- [v0.2.1](release_notes/Viper_Release_Notes_0_2_1.md)
- [v0.2.0](release_notes/Viper_Release_Notes_0_2_0.md)
- [v0.1.3](release_notes/Viper_Release_Notes_0_1_3.md)
- [v0.1.2](release_notes/Viper_Release_Notes_0_1_2.md)

---

## Architecture Decision Records

- [ADR-0001](adr/0001-builtin-signatures-from-registry.md) — Runtime signatures from centralized registry
- [ADR-0002](adr/0002-threads-monitor-safe.md) — Thread-safety for Monitor
- [ADR-0003](adr/0003-il-linkage-and-module-linking.md) — IL module linking
