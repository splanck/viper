<p align="center">
  <img src="misc/images/viperlogo2.png" alt="Viper" width="160">
</p>

<h1 align="center">Viper</h1>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPL v3"></a>
  <img src="https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-brightgreen" alt="Platform">
</p>

**Viper** is an IL-first compiler toolchain and virtual machine for building platform-native applications and games. Programs compile through a strongly typed, SSA-based intermediate language (**[Viper IL](docs/il-guide.md)**) that can be executed by the [VM](docs/vm.md) or compiled directly to native machine code.

**[Zia](docs/zia-reference.md)** is Viper's flagship language — a modern, statically typed language with classes, generics, enums, lambdas, modules, and pattern matching, designed for building real applications and games on the Viper platform. A **[BASIC](docs/basic-reference.md)** frontend is also included for educational use and rapid prototyping.

> **Status:** Active development. APIs, IL, and tooling are still evolving and are subject to change. Ready for experimentation and game prototyping, but not production-ready.

---

## Download

**Latest Release:** [v0.2.5-dev](https://github.com/splanck/viper/releases/tag/v0.2.5-dev) (5/7/2026)

- [Source (tar.gz)](https://github.com/splanck/viper/archive/refs/tags/v0.2.5-dev.tar.gz)
- [Source (zip)](https://github.com/splanck/viper/archive/refs/tags/v0.2.5-dev.zip)
- [Release Notes](https://github.com/splanck/viper/releases/tag/v0.2.5-dev)

**In development:** v0.2.6 (unreleased) — See the [draft release notes](docs/release_notes/Viper_Release_Notes_0_2_6.md) for the full changelog.

> **Working with the latest code:** The `master` branch is a live snapshot of current
> development and is ahead of v0.2.5. To work with the most recent code:
>
> ```bash
> git clone https://github.com/splanck/viper.git
> cd viper
> ```

---

## Quickstart

Build and test:

```bash
# macOS
./scripts/build_viper_mac.sh

# Linux
./scripts/build_viper_linux.sh

# Windows
.\scripts\build_viper.cmd
```

Create and run a project:

```bash
viper init my-app              # Zia project (default)
viper init my-app --lang basic # BASIC project
viper run my-app
```

Try the interactive [REPL](docs/repl.md):

```bash
viper repl
```

```
zia> Say("Hello from the REPL")
Hello from the REPL
zia> Say(Fmt.Int(2 + 3))
5
```

> See the **[Getting Started Guide](docs/getting-started.md)** for full setup instructions.

---

## Components

| Component | Description |
|-----------|-------------|
| **[Zia](docs/zia-reference.md)** | Modern, statically typed language with classes, generics, enums, lambdas, modules, and pattern matching |
| **[BASIC](docs/basic-reference.md)** | Educational frontend for rapid prototyping |
| **[Viper IL](docs/il-guide.md)** | Typed, SSA-based intermediate representation |
| **[Optimizer](docs/il-passes.md)** | 24-pass pipeline: GVN, LICM, SCCP, loop opts, inlining, devirtualization, ownership-pair removal, runtime fast paths |
| **[VM](docs/vm.md)** | Bytecode interpreter with switch, table, and threaded dispatch |
| **[AArch64](docs/codegen/aarch64.md) · [x86-64](docs/codegen/x86_64.md)** | Native code generators |
| **[Assembler](docs/codegen/native-assembler.md) · [Linker](docs/codegen/native-linker.md)** | Built-in ELF/Mach-O/PE toolchain — zero external dependencies |
| **[Runtime](docs/viperlib/README.md)** | 393 classes across 21 modules (graphics, 3D, GUI, game engine, networking, localization, and more) |
| **[Language Servers](docs/zia-server.md)** | Dual-protocol (LSP + MCP) servers for Zia and BASIC |
| **[Tools](docs/tools.md)** | Compiler drivers, verifier, disassembler, [REPL](docs/repl.md), packager |

### Why Viper?

- **Platform-native** — [Zia](docs/zia-reference.md) compiles to native machine code via [Viper IL](docs/il-guide.md) — no VM required for production
- **IL-centric** — A readable, typed IR makes semantics explicit and frontends interchangeable
- **Self-contained** — Built-in [assembler](docs/codegen/native-assembler.md) and [linker](docs/codegen/native-linker.md) with ELF/Mach-O/PE support and dynamic linking — zero external tool dependencies for native compilation
- **Memory-safe surface** — [Zia](docs/zia-reference.md) and [BASIC](docs/basic-reference.md) expose no raw pointer types or pointer-signature runtime APIs; the typed, lifetime-validated surface is the only surface
- **Full runtime** — 393 classes covering [graphics](docs/viperlib/graphics/README.md), [3D](docs/graphics3d-guide.md), [networking](docs/viperlib/network.md), [GUI](docs/viperlib/gui/README.md), [threading](docs/viperlib/threads.md), [localization](docs/viperlib/localization/README.md), and more

---

## Project Status

Viper is in **active development**. All components are functional but evolving:

| Component | Notes |
|-----------|-------|
| [Zia Frontend](docs/zia-reference.md) | Classes, structs, generics with constraints, enums, lambdas, pattern matching, default interface methods, `Result`/`Unit`, structured try/catch/finally + defer, declaration-order independence, modules |
| [BASIC Frontend](docs/basic-reference.md) | Core language with OOP, enums, select-case, namespaces |
| [Viper IL](docs/il-guide.md) | Stable core; module linker for cross-language interop |
| [Optimizer](docs/il-passes.md) | 24-pass pipeline covering SSA, loop, inlining, devirtualization, ownership, and peephole opts |
| [VM](docs/vm.md) | Switch / table / threaded dispatch with trap handling and worker VMs |
| [AArch64 Backend](docs/codegen/aarch64.md) | Apple Silicon and Windows ARM64 with register coalescing and post-RA scheduling |
| [x86-64 Backend](docs/codegen/x86_64.md) | Windows and Linux, IEEE-754 NaN-safe, 300+ stress tests |
| [Native Toolchain](docs/codegen/native-assembler.md) | In-tree assembler and linker for ELF / Mach-O / PE with DWARF v5 and dynamic linking |
| [Runtime](docs/viperlib/README.md) | 393 classes across 21 modules |
| [3D Graphics](docs/graphics3d-guide.md) | 46 classes covering meshes, materials, lighting, skeletal animation, terrain, water, physics, asset import (glTF / FBX), and post-processing across Metal / D3D11 / OpenGL / software backends |
| [Game Engine](docs/viperlib/game/README.md) | Collision, pathfinding, physics, tweening, particles, state machines, AI behaviors, level loading, asset embedding |
| [GUI](docs/viperlib/gui/README.md) | 47 widgets for cross-platform desktop apps |
| [IDE / Language Servers](docs/zia-server.md) | ViperIDE with diagnostics, hover, go-to-definition, IntelliSense, symbol search; LSP + MCP servers |
| [Packaging](docs/tools.md) | `viper package` for apps, `viper install-package` for the toolchain |

Expect breaking changes. The IL specification, APIs, and tool interfaces are not stable.

---

## Demos

| Demo | Description |
|------|-------------|
| [ViperSQL](examples/apps/vipersql/) | SQL database engine with an interactive client |
| [Paint](examples/apps/paint/) | Drawing app with layers, undo/redo, and an expanded tool set |
| [Chess](examples/games/chess/) | Chess with alpha-beta AI and a drag-and-drop GUI |
| [Crackman](examples/games/crackman/) | Maze chase game with ghost AI |
| [XENOSCAPE](examples/games/xenoscape/) | Metroid-style sidescroller with bosses, abilities, and saves |
| [3D Bowling](examples/games/3dbowling/) | Physics-driven 3D bowling with multi-mode camera |

> **[See all demos →](examples/README.md)** — 6 applications, 17 games, API coverage audits, IL examples, and C++ embedding demos.

---

## Architecture

```
┌─────────────────────────────────────────┐
│           Source Languages              │
│          (Zia · BASIC · ...)            │
└─────────────────┬───────────────────────┘
                  ▼
┌─────────────────────────────────────────┐
│    Frontend (Parser · Sema · Lowerer)   │
└─────────────────┬───────────────────────┘
                  ▼
┌─────────────────────────────────────────┐
│         Viper IL  (Typed SSA)           │
│      Verifier · Optimizer (24 passes)   │
└─────────┬───────────────┬───────────────┘
          ▼               ▼
┌──────────────┐   ┌──────────────────────┐
│   Bytecode   │   │   Native Backends    │
│      VM      │   │   (AArch64 · x86-64) │
│              │   ├──────────────────────┤
│              │   │  Assembler · Linker  │
│              │   │  (ELF · Mach-O · PE) │
└──────┬───────┘   └──────────┬───────────┘
       └──────────┬───────────┘
                  ▼
┌─────────────────────────────────────────┐
│            Viper Runtime                │
│  Collections · Graphics · 3D · GUI ·    │
│  Game Engine · Audio · Network · I/O ·  │
│  Threads · Crypto · Math · Text ·       │
│  Localization · ...                     │
└─────────────────────────────────────────┘
```

> See [Architecture Overview](docs/architecture.md) and [Code Map](docs/codemap.md) for detailed design.

---

## IL at a Glance

Frontends lower to a typed [IL](docs/il-guide.md) that is compact, explicit, and inspectable.

**Zia Source:**

```rust
module Hello;

bind Viper.Terminal;
bind Fmt = Viper.Text.Fmt;

func start() {
    var x = 2 + 3;
    var y = x * 2;
    Say("HELLO");
    Say(Fmt.Int(y));
}
```

**Viper IL Output:**

```llvm
il 0.2.0
extern @Viper.Text.Fmt.Int(i64) -> str
extern @Viper.Terminal.Say(str) -> void
global const str @.L0 = "HELLO"
func @main() -> void {
entry_0:
  %t0 = iadd.ovf 2, 3
  %t1 = alloca 8
  store i64, %t1, %t0
  %t2 = load i64, %t1
  %t3 = imul.ovf %t2, 2
  %t4 = alloca 8
  store i64, %t4, %t3
  %t5 = const_str @.L0
  call @Viper.Terminal.Say(%t5)
  %t6 = load i64, %t4
  %t7 = call @Viper.Text.Fmt.Int(%t6)
  call @Viper.Terminal.Say(%t7)
  ret
}
```

> See the **[IL Quickstart](docs/il-quickstart.md)** for a hands-on introduction, or the **[IL Reference](docs/il-guide.md)** for the full specification.

---

## Runtime Library

All frontends share the **[Viper Runtime](docs/viperlib/README.md)** — 393 classes across 21 modules:

| Module | Classes | Description |
|--------|:-------:|-------------|
| [Collections](docs/viperlib/collections) | 29 | General-purpose data structures |
| [Core](docs/viperlib/core.md) | 7 | Base types and string operations |
| [Crypto](docs/viperlib/crypto.md) | 8 | Symmetric and asymmetric ciphers, hashing, key derivation, secure RNG |
| [Data](docs/viperlib/README.md) | 3 | Structured data serialization |
| [Game](docs/viperlib/game/README.md) | 25 | High-level 2D game systems, AI behaviors, level and scene management |
| [Game.Physics2D](docs/viperlib/game/physics.md) | 8 | 2D rigid-body dynamics and joints |
| [Game.UI](docs/viperlib/game/ui.md) | 12 | In-game HUD widgets |
| [Graphics](docs/viperlib/graphics/README.md) | 57 | 2D rendering, sprites, tilemaps, fonts, and the production 2D class set (render targets, textures, shaders, nine-slice, paths, debug draw) |
| [Graphics3D](docs/graphics3d-guide.md) | 46 | Full 3D pipeline: meshes, materials, lighting, skeletal and node animation, physics, terrain, water, asset import |
| [GUI](docs/viperlib/gui/README.md) | 47 | Cross-platform desktop widgets |
| [I/O](docs/viperlib/io) | 16 | Files, archives, compression, streaming |
| [Input](docs/viperlib/input.md) | 6 | Keyboard, mouse, gamepad, and action mapping |
| [Localization](docs/viperlib/localization/README.md) | 10 | BCP-47 locales, locale-aware number and date formatting, message bundles, CLDR-subset plural rules, list formatting, text direction |
| [Math](docs/viperlib/math.md) | 12 | Linear algebra, noise, splines, arbitrary precision |
| [Memory](docs/memory-management.md) | 3 | Validated retain/release wrappers and GC controls |
| [Network](docs/viperlib/network.md) | 27 | HTTP/1.1 and HTTP/2 client and server, WebSocket, TLS (in-tree X.509, RSA, ECDSA), UDP, SSE, connection pooling |
| [Sound](docs/viperlib/audio.md) | 8 | Playback, synthesis, playlists, sound banks |
| [Text](docs/viperlib/text) | 20 | Structured-text parsing, templates, regex |
| [Threads](docs/viperlib/threads.md) | 18 | Async primitives, channels, futures, pools |
| [Time](docs/viperlib/time.md) | 8 | Clocks, dates, durations, timers |
| [Utilities](docs/viperlib/README.md) | 23 | Formatting, logging, option / result helpers |

> See the **[Runtime Library Reference](docs/viperlib/README.md)** for complete API documentation.

---

## Tools

| Tool | Purpose |
|------|---------|
| `viper` | Unified driver — [run, build, compile, package](docs/tools.md) |
| `viper repl` | [Interactive REPL](docs/repl.md) for Zia and BASIC |
| `viper package` | Generate installers (.app, .deb, .exe, .tar.gz) |
| `viper install-package` | Generate staged toolchain installers (.exe, .pkg, .deb, .rpm, self-installing .tar.gz) |
| `zia` / `vbasic` | Standalone language compilers |
| `zia-server` | [Language server](docs/zia-server.md) (LSP + MCP) |
| `viper -run` | Execute IL programs directly |
| `viper il-opt` | [Optimize](docs/il-passes.md) and verify IL (`-verify-each`) |
| `viper bench` | IL benchmark runner |

```bash
viper run program.zia          # Run source
viper build project/ -o app    # Compile to native binary
viper repl                     # Interactive REPL
```

> See **[Tools Reference](docs/tools.md)** for all commands and flags.

---

## Building

### Requirements

- CMake 3.20+
- C++20 compiler (Clang recommended, GCC 11+, or MSVC)

### Build Steps

```bash
# macOS
./scripts/build_viper_mac.sh

# Linux
./scripts/build_viper_linux.sh

# Windows
.\scripts\build_viper.cmd
```

This configures, builds, tests, and installs Viper in one step.

### Platform Notes

- **[macOS](docs/getting-started/macos.md)** — Use Apple Clang. ARM64 tests skip x86-64-specific checks automatically.
- **[Linux](docs/getting-started/linux.md)** — Clang recommended.
- **[Windows](docs/getting-started/windows.md)** — MSVC is the default compiler on Windows.

---

## Documentation

**Getting Started** — [Setup Guide](docs/getting-started.md) · [REPL Guide](docs/repl.md) · [Zia Tutorial](docs/zia-getting-started.md) · [The Viper Bible](docs/bible/README.md)

**Language References** — [Zia Reference](docs/zia-reference.md) · [BASIC Reference](docs/basic-reference.md) · [IL Guide](docs/il-guide.md) · [IL Quickstart](docs/il-quickstart.md)

**Runtime & APIs** — [Runtime Library](docs/viperlib/README.md) · [3D Graphics](docs/graphics3d-guide.md) · [Game Engine](docs/gameengine/README.md) · [GUI](docs/viperlib/gui/README.md)

**Internals** — [Architecture](docs/architecture.md) · [VM Design](docs/vm.md) · [Code Map](docs/codemap.md) · [Backend](docs/backend.md) · [IL Passes](docs/il-passes.md)

**For Contributors** — [Contributor Guide](docs/contributor-guide.md) · [Frontend How-To](docs/frontend-howto.md) · [Testing](docs/testing.md)

> Browse the full **[docs/](docs/)** hierarchy for 200+ documents.

---

## Contributing

Viper is in active development and the architecture is stabilizing. We welcome:

- Bug reports and issues
- Small fixes and documentation improvements
- Feedback and suggestions

We are not currently seeking large feature PRs while the design solidifies. Feel free to fork for broader experimentation.

---

## License

Viper is licensed under the **GNU General Public License v3.0** (GPL-3.0-only).

See [LICENSE](LICENSE) for the full text.
