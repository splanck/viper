---
status: active
audience: public
last-verified: 2026-07-16
---

# Viper Documentation

Viper is a from-scratch compiler toolchain and game development platform: two language frontends ([Zia](languages/zia-reference.md) and [BASIC](languages/basic-reference.md)) lower to a shared [intermediate language](il/il-guide.md) that runs on a [VM](internals/vm.md) or compiles to [native code](internals/backend.md), on top of a comprehensive [runtime library](viperlib/README.md).

> Contributor and internals documentation (architecture, code maps, specs, ADRs) lives in the **[Internals index](internals/README.md)**.

---

## Getting Started

- [Getting Started](getting-started.md) — Build, install, and run your first program
- Platform setup: [macOS](getting-started/macos.md) · [Linux](getting-started/linux.md) · [Windows](getting-started/windows.md)
- [FAQ](faq.md) — Common questions answered
- [Installer and Package Release Guide](installer-release.md) — Native artifacts, signing, verification, and lifecycle validation

## Learning Viper

- [Zia Tutorial](tutorials/zia-tutorial.md) — Learn Zia by example
- [BASIC Tutorial](tutorials/basic-tutorial.md) — Learn Viper BASIC by example
- [The Viper Book](book/README.md) — A five-part course from first program to full applications

## Language References

- **Zia** — [Language Reference](languages/zia-reference.md) · [Grammar](languages/zia-grammar.md)
- **BASIC** — [Language Reference](languages/basic-reference.md) · [Namespaces](languages/basic-namespaces.md) · [Grammar Notes](languages/basic-grammar.md) · [Lifetime Model](languages/lifetime.md)
- **Cross-language** — [Interop](languages/interop.md) · [Arithmetic Semantics](languages/arithmetic-semantics.md)
- **Viper IL** — [IL Guide](il/il-guide.md) (normative; includes quickstart and full reference) · [Optimization Passes](il/il-passes.md)

## Runtime Library

- [Runtime Library Index](viperlib/README.md) — All `Viper.*` classes and methods
- [Generated API Reference](generated/runtime/README.md) — Exhaustive, source-generated class inventory
- [Runtime Architecture](viperlib/architecture.md) — Runtime design and structure
- [Memory Management](memory-management.md) — GC, allocation, and object lifetime

### Module Reference

**[Collections](viperlib/collections/README.md)** — [Sequential](viperlib/collections/sequential.md) · [Maps & Sets](viperlib/collections/maps-sets.md) · [Multi-Maps](viperlib/collections/multi-maps.md) · [Specialized](viperlib/collections/specialized.md) · [Functional](viperlib/collections/functional.md)

**[Core](viperlib/core.md)** · **[Crypto](viperlib/crypto.md)** · **[Diagnostics](viperlib/diagnostics.md)** · **[Functional](viperlib/functional.md)**

**[Game](viperlib/game/README.md)** — [Core](viperlib/game/core.md) · [Game Loop](viperlib/game/gameloop.md) · [Animation](viperlib/game/animation.md) · [Effects](viperlib/game/effects.md) · [Pathfinding](viperlib/game/pathfinding.md) · [Physics](viperlib/game/physics.md) · [Persistence](viperlib/game/persistence.md) · [Debug](viperlib/game/debug.md) · [UI](viperlib/game/ui.md)

**[Graphics](viperlib/graphics/README.md)** — [Canvas](viperlib/graphics/canvas.md) · [Scene](viperlib/graphics/scene.md) · [Pixels](viperlib/graphics/pixels.md) · [Fonts](viperlib/graphics/fonts.md)

**[Game3D](viperlib/graphics/game3d.md)** — code-first 3D game workflow helpers over `Viper.Graphics3D`

**[Graphics3D](graphics3d-guide.md)** — 3D rendering, physics, and world systems guide

**[GUI](viperlib/gui/README.md)** — [Application](viperlib/gui/application.md) · [Core](viperlib/gui/core.md) · [Widgets](viperlib/gui/widgets.md) · [Containers](viperlib/gui/containers.md) · [Layout](viperlib/gui/layout.md)

**[I/O](viperlib/io/README.md)** — [Files](viperlib/io/files.md) · [Streams](viperlib/io/streams.md) · [Advanced](viperlib/io/advanced.md)

**[Audio](viperlib/audio.md)** · **[Input](viperlib/input.md)** · **[Localization](viperlib/localization/README.md)** · **[Math](viperlib/math.md)** · **[Network](viperlib/network.md)**

**[System](viperlib/system.md)** · **[Text](viperlib/text/README.md)** — [Formats](viperlib/text/formats.md) · [Formatting](viperlib/text/formatting.md) · [Encoding](viperlib/text/encoding.md) · [Patterns](viperlib/text/patterns.md)

**[Threads](viperlib/threads.md)** · **[Time](viperlib/time.md)** · **[Utilities](viperlib/utilities.md)** · **[Zia Tooling](viperlib/zia.md)**

**[Graphics Library (2D)](graphics-library.md)** — ViperGFX low-level 2D API

## Game Engine

- [Game Engine Documentation](gameengine/README.md) — Complete guide to the Viper game engine
- [Getting Started](gameengine/getting-started.md) — Your first game in 15 minutes
- [Architecture](gameengine/architecture.md) — Engine systems and zero-dependency design
- [Example Games](gameengine/examples/README.md) — Example games from arcade to Metroidvania

## Tools

- [CLI Tools Reference](tools/cli.md) — `viper`, `zia`, `vbasic`, `ilrun`, `il-verify`, `il-dis`
- [REPL](tools/repl.md) — Interactive Zia/BASIC environment
- [Debugging Guide](tools/debugging.md) — VM tracing, breakpoints, and diagnostics
- [Language Server](tools/zia-server.md) — `zia-server` for AI assistants and editors (LSP + MCP)
- [MCP Tools Reference](tools/zia-server-mcp-tools.md) — Detailed MCP tool definitions

## Release Notes

Current development line: **v0.2.99** ([draft notes](release_notes/Viper_Release_Notes_0_2_99.md)).

[v0.2.7](release_notes/Viper_Release_Notes_0_2_7.md) · [v0.2.6](release_notes/Viper_Release_Notes_0_2_6.md) · [v0.2.5](release_notes/Viper_Release_Notes_0_2_5.md) · [v0.2.4](release_notes/Viper_Release_Notes_0_2_4.md) · [v0.2.3](release_notes/Viper_Release_Notes_0_2_3.md) · [v0.2.2](release_notes/Viper_Release_Notes_0_2_2.md) · [v0.2.1](release_notes/Viper_Release_Notes_0_2_1.md) · [v0.2.0](release_notes/Viper_Release_Notes_0_2_0.md) · [v0.1.3](release_notes/Viper_Release_Notes_0_1_3.md) · [v0.1.2](release_notes/Viper_Release_Notes_0_1_2.md)

---

## For Contributors

The **[Internals index](internals/README.md)** covers system architecture, the VM and native backends, code maps for every subsystem, formal specifications, architecture decision records, testing, and how-to guides for extending the runtime and building new frontends.

- [Windows Runtime Reliability Audit](windows-runtime-reliability-audit.md) — Win32 and D3D11 adapter hardening, regression coverage, and validation.
