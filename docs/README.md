---
status: active
audience: public
last-verified: 2026-06-27
---

# Viper Documentation

Documentation for the Viper compiler toolchain: two language frontends ([Zia](zia-reference.md) and [BASIC](basic-reference.md)), a shared [intermediate language](il-guide.md), a [VM](vm.md), [native code generation](backend.md) backends, and a comprehensive [runtime library](viperlib/README.md).

---

## 🚀 Getting Started

- [Getting Started](getting-started.md) — Build, install, and run your first program
- [macOS Setup](getting-started/macos.md) · [Linux Setup](getting-started/linux.md) · [Windows Setup](getting-started/windows.md)
- [FAQ](faq.md) — Common questions answered

---

## 📖 Language Tutorials

- [Zia Tutorial](zia-getting-started.md) — Learn Zia by example
- [BASIC Tutorial](basic-language.md) — Learn Viper BASIC by example
- [The Viper Bible](bible/README.md) — Comprehensive 5-part book: Foundations → Building Blocks → Objects → Applications → Mastery

---

## 📝 Language References

### Zia

- [Zia Reference](zia-reference.md) — Complete language specification
- [Feature Parity Matrix](feature-parity.md) — Zia vs BASIC feature comparison

### BASIC

- [BASIC Reference](basic-reference.md) — Complete language specification
- [Namespace Reference](basic-namespaces.md) — BASIC namespace system
- [Grammar Notes](basic-grammar.md) — BASIC grammar extensions
- [Lifetime Model](lifetime.md) — Reference counting and disposal

### Cross-Language

- [Cross-Language Interop](interop.md) — Zia + BASIC IL linking, type compatibility
- [Arithmetic Semantics](arithmetic-semantics.md) — Overflow, rounding, and numeric guarantees

---

## 🔬 Intermediate Language (IL)

- [IL Guide](il-guide.md) — Comprehensive specification (normative)
- [IL Quickstart](il-quickstart.md) — Fast introduction for developers
- [IL Reference](il-reference.md) — Opcode and type reference
- [IL Passes](il-passes.md) — Optimization pass infrastructure

---

## 📚 Runtime Library

- [Runtime Library Index](viperlib/README.md) — All `Viper.*` classes and methods
- [Runtime Architecture](viperlib/architecture.md) — Runtime design and structure
- [Runtime Extension How-To](runtime_extend_howto.md) — Add new classes and functions
- [Adding a Runtime Class](runtime_class_howto.md) — Deep dive: 8-step guide
- [Memory Management](memory-management.md) — GC, allocation, and object lifetime

### Module Reference

**[Collections](viperlib/collections/README.md)** — [Sequential](viperlib/collections/sequential.md) · [Maps & Sets](viperlib/collections/maps-sets.md) · [Multi-Maps](viperlib/collections/multi-maps.md) · [Specialized](viperlib/collections/specialized.md) · [Functional](viperlib/collections/functional.md)

**[Core](viperlib/core.md)** · **[Crypto](viperlib/crypto.md)** · **[Diagnostics](viperlib/diagnostics.md)** · **[Functional](viperlib/functional.md)**

**[Game](viperlib/game/README.md)** — [Core](viperlib/game/core.md) · [Game Loop](viperlib/game/gameloop.md) · [Animation](viperlib/game/animation.md) · [Effects](viperlib/game/effects.md) · [Pathfinding](viperlib/game/pathfinding.md) · [Physics](viperlib/game/physics.md) · [Persistence](viperlib/game/persistence.md) · [Debug](viperlib/game/debug.md) · [UI](viperlib/game/ui.md)

**[Graphics](viperlib/graphics/README.md)** — [Canvas](viperlib/graphics/canvas.md) · [Scene](viperlib/graphics/scene.md) · [Pixels](viperlib/graphics/pixels.md) · [Fonts](viperlib/graphics/fonts.md)

**[Game3D](viperlib/graphics/game3d.md)** — code-first 3D game workflow helpers over `Viper.Graphics3D`

**[Graphics3D](graphics3d-guide.md)** — [Guide](graphics3d-guide.md) · [Architecture](graphics3d-architecture.md)

**[GUI](viperlib/gui/README.md)** — [Application](viperlib/gui/application.md) · [Core](viperlib/gui/core.md) · [Widgets](viperlib/gui/widgets.md) · [Containers](viperlib/gui/containers.md) · [Layout](viperlib/gui/layout.md)

**[I/O](viperlib/io/README.md)** — [Files](viperlib/io/files.md) · [Streams](viperlib/io/streams.md) · [Advanced](viperlib/io/advanced.md)

**[Input](viperlib/input.md)** · **[Math](viperlib/math.md)** · **[Network](viperlib/network.md)** · **[Sound](viperlib/audio.md)**

**[System](viperlib/system.md)** · **[Text](viperlib/text/README.md)** — [Formats](viperlib/text/formats.md) · [Formatting](viperlib/text/formatting.md) · [Encoding](viperlib/text/encoding.md) · [Patterns](viperlib/text/patterns.md)

**[Threads](viperlib/threads.md)** · **[Time](viperlib/time.md)** · **[Utilities](viperlib/utilities.md)** · **[Zia Tooling](viperlib/zia.md)**

**[Graphics Library (2D)](graphics-library.md)** — ViperGFX low-level 2D API

---

## 🎮 Game Engine

- [Game Engine Documentation](gameengine/README.md) — Complete guide to the Viper game engine
- [Getting Started](gameengine/getting-started.md) — Your first game in 15 minutes
- [Architecture](gameengine/architecture.md) — Engine systems and zero-dependency design
- [Example Games](gameengine/examples/README.md) — Example games from arcade to Metroidvania

---

## 🔧 Tools & CLI

- [CLI Tools Reference](tools.md) — `viper`, `zia`, `vbasic`, `ilrun`, `il-verify`, `il-dis`
- [Language Server](zia-server.md) — `zia-server` for AI assistants and editors (LSP + MCP)
- [MCP Tools Reference](zia-server-mcp-tools.md) — Detailed MCP tool definitions
- [REPL](repl.md) — Interactive Zia/BASIC environment
- [Debugging Guide](debugging.md) — VM tracing, breakpoints, and diagnostics
- [Testing Guide](testing.md) — Unit, golden, e2e, and performance tests
- [Source Health Guardrails](source-health.md) — Local audit baselines for high-ownership subsystems
- [Review Readiness](review-readiness.md) — Pre-review validation checklist and platform-claim rules

---

## 🏗️ Architecture & Internals

- [Architecture](architecture.md) — System design: frontends, IL, VM, codegen
- [Code Map](codemap.md) — Source directory layout and subsystem overview
- [VM Guide](vm.md) — VM design, dispatch, profiling, and runtime ABI
- [Bytecode VM Reference](BYTECODE_VM_DESIGN.md) — Current bytecode format, dispatch, runtime, and test surface
- [Backend Guide](backend.md) — x86-64 and ARM64 native code generation
- [Threading and Globals](threading-and-globals.md) — VM threading model
- [Generated Files](generated-files.md) — Auto-generated C++ sources
- [Dependencies](dependencies.md) — Build and runtime dependencies

### Native Toolchain

- [Native Assembler](codegen/native-assembler.md) — Binary encoders and object file writers (x86_64/AArch64 × ELF/Mach-O/COFF)
- [Native Linker](codegen/native-linker.md) — Archive reading, symbol resolution, relocation, and executable output

### Code Map Deep Dives

**IL Subsystem** — [Core](codemap/il-core.md) · [Build](codemap/il-build.md) · [I/O](codemap/il-i-o.md) · [Transform](codemap/il-transform.md) · [Analysis](codemap/il-analysis.md) · [Link](codemap/il-link.md) · [Runtime](codemap/il-runtime.md) · [API](codemap/il-api.md) · [Verification](codemap/il-verification.md) · [Utilities](codemap/il-utilities.md)

**Frontends** — [Zia](codemap/front-end-zia.md) · [BASIC](codemap/front-end-basic.md) · [Common](codemap/front-end-common.md)

**Other** — [Bytecode VM](codemap/bytecode-vm.md) · [Codegen](codemap/codegen.md) · [Graphics](codemap/graphics.md) · [Graphics Stubs](codemap/runtime-graphics-stubs.md) · [VM/Runtime](codemap/vm-runtime.md) · [Support](codemap/support.md) · [Tools](codemap/tools.md) · [TUI](codemap/tui.md) · [Zia Server](codemap/zia-server.md) · [Docs](codemap/docs.md)

---

## 🌐 Cross-Platform

- [Platform Differences](cross-platform/platform-differences.md) — macOS vs Linux vs Windows behavior
- [Platform Checklist](cross-platform/platform-checklist.md) — Cross-platform compliance tracking

---

## 📐 Specifications

- [Specifications Index](specifications.md) — Overview of all formal specs
- [Error Model](specs/errors.md) — Trap kinds, handler semantics, error model
- [Numeric Types](specs/numerics.md) — Numeric types, ranges, IEEE semantics
- [Object Layout / ABI](abi/object-layout.md) — Object layout, vtable, call ABI
- [AArch64 Backend](codegen/aarch64.md) — AArch64 backend specification
- [x86-64 Backend](codegen/x86_64.md) — x86-64 SysV ABI reference
- [Requirements](requirements.md) — Project requirements and constraints

---

## 🎓 The Viper Bible

A comprehensive learning resource organized as a 5-part book covering the entire Viper platform.

- [Bible Index](bible/README.md) — Table of contents and chapter list

---

## 📋 Release Notes

- [v0.2.7](release_notes/Viper_Release_Notes_0_2_7.md) *(DRAFT)*
- [v0.2.6](release_notes/Viper_Release_Notes_0_2_6.md)
- [v0.2.5](release_notes/Viper_Release_Notes_0_2_5.md)
- [v0.2.4](release_notes/Viper_Release_Notes_0_2_4.md)
- [v0.2.3](release_notes/Viper_Release_Notes_0_2_3.md)
- [v0.2.2](release_notes/Viper_Release_Notes_0_2_2.md)
- [v0.2.1](release_notes/Viper_Release_Notes_0_2_1.md)
- [v0.2.0](release_notes/Viper_Release_Notes_0_2_0.md)
- [v0.1.3](release_notes/Viper_Release_Notes_0_1_3.md)
- [v0.1.2](release_notes/Viper_Release_Notes_0_1_2.md)

---

## 📌 Architecture Decision Records

- [ADR-0001](adr/0001-builtin-signatures-from-registry.md) — Runtime signatures from centralized registry
- [ADR-0002](adr/0002-threads-monitor-safe.md) — Thread-safety for Monitor
- [ADR-0003](adr/0003-il-linkage-and-module-linking.md) — IL module linking
- [ADR-0004](adr/0004-graphics3d-runtime-surface-expansion.md) — Graphics3D runtime surface expansion
- [ADR-0005](adr/0005-resume-token-provenance.md) — Resume-token provenance
- [ADR-0006](adr/0006-spec-currency-and-adr-triggers.md) — Spec currency and ADR triggers
- [ADR-0007](adr/0007-codeeditor-syntax-surface-expansion.md) — CodeEditor syntax surface expansion
- [ADR-0008](adr/0008-semantic-token-overlay.md) — Semantic token overlay
- [ADR-0009](adr/0009-debug-evaluate-protocol.md) — Debug adapter evaluate protocol
- [ADR-0010](adr/0010-workspace-file-index-status.md) — Workspace file index status API
- [ADR-0011](adr/0011-codeeditor-editing-runtime-api.md) — CodeEditor editing runtime API
- [ADR-0012](adr/0012-debug-conditional-breakpoints-logpoints.md) — Debug conditional breakpoints and logpoints
- [ADR-0013](adr/0013-editor-input-popup-runtime-surface.md) — Editor input and popup runtime surface
- [ADR-0014](adr/0014-basic-language-service-runtime-bridge.md) — Viper BASIC language-service runtime bridge
- [ADR-0015](adr/0015-workspace-file-index-paging.md) — Workspace file index paging
- [ADR-0016](adr/0016-pty-runtime-surface.md) — PTY runtime surface
- [ADR-0017](adr/0017-string-lines-runtime-function.md) — String lines runtime function

---

## 👥 Contributing

- [Contributor Guide](contributor-guide.md) — Style guide and contribution process
- [Frontend How-To](frontend-howto.md) — Build your own language frontend
