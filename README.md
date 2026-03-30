<p align="center">
  <img src="misc/images/viperlogo2.png" alt="Viper" width="160">
</p>

<h1 align="center">Viper</h1>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPL v3"></a>
  <img src="https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-brightgreen" alt="Platform">
  <img src="https://img.shields.io/badge/SLOC-391K-orange" alt="SLOC">
</p>

**Viper** is an IL-first compiler toolchain and virtual machine for building platform-native applications. Programs compile through a strongly typed, SSA-based intermediate language (**[Viper IL](docs/il-guide.md)**) that can be executed by the [VM](docs/vm.md) or compiled directly to native machine code.

**[Zia](docs/zia-reference.md)** is Viper's flagship language — a modern, statically typed language with classes, generics, enums, modules, and pattern matching, designed for building real applications on the Viper platform. A **[BASIC](docs/basic-reference.md)** frontend is also included for educational use and rapid prototyping.

> **Status:** Early development. APIs, IL, and tooling change frequently. Not production-ready.

---

## Download

**Latest Release:** [v0.2.3-dev](https://github.com/splanck/viper/releases/tag/v0.2.3-dev) (3/25/2026)

- [Source (tar.gz)](https://github.com/splanck/viper/archive/refs/tags/v0.2.3-dev.tar.gz)
- [Source (zip)](https://github.com/splanck/viper/archive/refs/tags/v0.2.3-dev.zip)
- [Release Notes](docs/release_notes/Viper_Release_Notes_0_2_3.md)

> **Working with the latest code:** The `master` branch is a live snapshot of current
> development and may be ahead of any tagged release. To work with the most recent code:
>
> ```bash
> git clone https://github.com/splanck/viper.git
> cd viper
> ```

---

## 🚀 Quickstart

Build and test:

```bash
./scripts/build_viper.sh
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

## 🧩 Components

| Component | Description |
|-----------|-------------|
| **[Zia](docs/zia-reference.md)** | Modern, statically typed language with classes, generics, enums, modules, and pattern matching |
| **[BASIC](docs/basic-reference.md)** | Educational frontend for rapid prototyping |
| **[Viper IL](docs/il-guide.md)** | Typed, SSA-based intermediate representation |
| **[Optimizer](docs/il-passes.md)** | 38-pass pipeline: GVN, LICM, SCCP, loop opts, inlining, and more |
| **[VM](docs/vm.md)** | Bytecode interpreter with switch, table, and threaded dispatch |
| **[AArch64](docs/codegen/aarch64.md) · [x86-64](docs/codegen/x86_64.md)** | Native code generators |
| **[Assembler](docs/codegen/native-assembler.md) · [Linker](docs/codegen/native-linker.md)** | Built-in ELF/Mach-O/PE toolchain — zero external dependencies |
| **[Runtime](docs/viperlib/README.md)** | 275+ classes across 22 modules (graphics, 3D, GUI, game engine, networking, and more) |
| **[Language Servers](docs/zia-server.md)** | Dual-protocol (LSP + MCP) servers for Zia and BASIC |
| **[Tools](docs/tools.md)** | Compiler drivers, verifier, disassembler, [REPL](docs/repl.md), packager |

### Why Viper?

- **Platform-native** — [Zia](docs/zia-reference.md) compiles to native machine code via [Viper IL](docs/il-guide.md) — no VM required for production
- **IL-centric** — A readable, typed IR makes semantics explicit and frontends interchangeable
- **Self-contained** — Built-in [assembler](docs/codegen/native-assembler.md) and [linker](docs/codegen/native-linker.md) — zero external tool dependencies for native compilation
- **Full runtime** — 275+ classes covering [graphics](docs/viperlib/graphics/README.md), [3D](docs/graphics3d-guide.md), [networking](docs/viperlib/network.md), [GUI](docs/viperlib/gui/README.md), [threading](docs/viperlib/threads.md), and more

---

## 📊 Project Status

Viper is in **early development**. All components are functional but evolving:

| Component | Notes |
|-----------|-------|
| [Zia Frontend](docs/zia-reference.md) | Classes, structs, generics, enums, pattern matching with exhaustiveness, try/catch, modules |
| [BASIC Frontend](docs/basic-reference.md) | Core language + OOP; enums, select-case, namespaces |
| [Viper IL](docs/il-guide.md) | Stable core; module linker for cross-language interop |
| [Optimizer](docs/il-passes.md) | 38 passes (SSA opts, loop opts, inlining, peephole, GVN, LICM, DSE) |
| [VM](docs/vm.md) | Switch, table, and threaded dispatch; SIGINT/SEH trap handling |
| [AArch64 Backend](docs/codegen/aarch64.md) | Apple Silicon + Windows ARM64; register coalescer, post-RA scheduler |
| [x86-64 Backend](docs/codegen/x86_64.md) | Windows + Linux; 300+ stress tests, IEEE 754 NaN-safe |
| [Native Toolchain](docs/codegen/native-assembler.md) | Assembler (ELF/Mach-O/COFF) + linker (dead stripping, ICF, DWARF v5, code signing) |
| [Runtime](docs/viperlib/README.md) | 275+ classes across 22 modules; 1,358 tests |
| [3D Graphics](docs/graphics3d-guide.md) | 28 classes; Metal (94% parity), D3D11 (20 features), OpenGL GPU backends + software fallback |
| [Game Engine](docs/viperlib/game/README.md) | Collision, pathfinding, physics, tweening, particles, state machines, UI widgets |
| [GUI](docs/viperlib/gui/README.md) | 46 widget classes; cross-platform desktop apps |
| [IDE / Language Servers](docs/zia-server.md) | ViperIDE demo; LSP + MCP protocol servers for both languages |
| [Packaging](docs/tools.md) | `viper package` → .app, .deb, .exe, .tar.gz |

Expect breaking changes. The IL specification, APIs, and tool interfaces are not stable.

---

## 🎮 Demos

| Demo | Description |
|------|-------------|
| [ViperIDE](examples/apps/viperide/) | IDE with tabs, IntelliSense, project tree, integrated build |
| [SQLdb](examples/apps/sqldb/) | SQL database engine with MVCC, WAL, B-tree indexes |
| [Paint](examples/apps/paint/) | Drawing app with 8 tools, shapes, color palette |
| [Chess](examples/games/chess/) | Chess with alpha-beta AI, transposition tables, drag-and-drop GUI |
| [Pac-Man](examples/games/pacman/) | Pac-Man with ghost AI, BFS pathfinding, scatter/chase modes |
| [XENOSCAPE](examples/games/sidescroller/) | Metroid-style sidescroller: 10 levels, 30+ enemies, bosses, abilities, saves (17K LOC) |
| [Dungeon of Viper](examples/games/dungeon/) | 3D first-person dungeon crawler using the Graphics3D engine |

> **[See all demos →](examples/README.md)** — 6 applications, 15 games, API coverage audits, IL examples, and C++ embedding demos.

---

## 🏗️ Architecture

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
│      Verifier · Optimizer (38 passes)   │
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
│  Threads · Crypto · Math · Text · ...   │
└─────────────────────────────────────────┘
```

> See [Architecture Overview](docs/architecture.md) and [Code Map](docs/codemap.md) for detailed design.

---

## 💡 IL at a Glance

Frontends lower to a typed [IL](docs/il-guide.md) that is compact, explicit, and inspectable.

**Zia Source:**

```rust
module Hello;

bind Viper.Terminal;
bind Fmt = Viper.Fmt;

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
extern @Viper.Fmt.Int(i64) -> str
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
  %t7 = call @Viper.Fmt.Int(%t6)
  call @Viper.Terminal.Say(%t7)
  ret
}
```

> See the **[IL Quickstart](docs/il-quickstart.md)** for a hands-on introduction, or the **[IL Reference](docs/il-guide.md)** for the full specification.

---

## 📚 Runtime Library

All frontends share the **[Viper Runtime](docs/viperlib/README.md)** — 275+ classes across 22 modules:

| Module | Classes | Description |
|--------|:-------:|-------------|
| [Collections](docs/viperlib/collections) | 29 | Lists, maps, sets, trees, heaps, queues, rings, tries |
| [Core](docs/viperlib/core.md) | 6 | Base types, string operations, messaging |
| [Crypto](docs/viperlib/crypto.md) | 10 | AES, AES-GCM, SHA, HMAC, HKDF, PBKDF2, TLS, ECDSA, secure RNG |
| [Data](docs/viperlib/README.md) | 3 | XML, YAML, binary serialization |
| [Game](docs/viperlib/game/README.md) | 22 | Collision, pathfinding, tweening, particles, state machines, platformer controller, lighting, achievements |
| [Game.Physics2D](docs/viperlib/game/physics.md) | 6 | Rigid bodies, joints (hinge, spring, rope, distance) |
| [Game.UI](docs/viperlib/game/ui.md) | 5 | In-game HUD widgets (bars, labels, menus, panels) |
| [Graphics](docs/viperlib/graphics/README.md) | 11 | Canvas, sprites, tilemaps, cameras, bitmap fonts |
| [Graphics3D](docs/graphics3d-guide.md) | 28 | Meshes, materials, lights, skeletal animation, physics, terrain, particles, FBX |
| [GUI](docs/viperlib/gui/README.md) | 46 | Desktop widgets, layouts, menus, code editor, tree views |
| [I/O](docs/viperlib/io) | 15 | Files, directories, archives, compression, streaming |
| [Input](docs/viperlib/input.md) | 6 | Keyboard, mouse, gamepad, action mapping |
| [Math](docs/viperlib/math.md) | 12 | Vectors, matrices, quaternions, noise, splines, BigInt |
| [Network](docs/viperlib/network.md) | 22 | HTTP, TCP, UDP, WebSocket, DNS, TLS, SMTP, SSE, connection pooling, rate limiting |
| [Sound](docs/viperlib/audio.md) | 7 | Audio playback, synthesis, playlists, sound banks |
| [Text](docs/viperlib/text) | 20 | JSON, TOML, CSV, XML, HTML, Markdown, templates, regex |
| [Threads](docs/viperlib/threads.md) | 18 | Async, channels, futures, pools, concurrent collections |
| [Time](docs/viperlib/time.md) | 8 | Clocks, dates, durations, countdowns, stopwatches |
| [Utilities](docs/viperlib/README.md) | 12 | Fmt, Log, Convert, Parse, Option, Result, Lazy |

> See the **[Runtime Library Reference](docs/viperlib/README.md)** for complete API documentation.

---

## 🔧 Tools

| Tool | Purpose |
|------|---------|
| `viper` | Unified driver — [run, build, compile, package](docs/tools.md) |
| `viper repl` | [Interactive REPL](docs/repl.md) for Zia and BASIC |
| `viper package` | Generate installers (.app, .deb, .exe, .tar.gz) |
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

## 🔨 Building

### Requirements

- CMake 3.20+
- C++20 compiler (Clang recommended, GCC 11+, or MSVC)

### Build Steps

```bash
./scripts/build_viper.sh
```

This configures, builds, tests, and installs Viper in one step.

### Platform Notes

- **[macOS](docs/getting-started/macos.md)** — Use Apple Clang. ARM64 tests skip x86-64-specific checks automatically.
- **[Linux](docs/getting-started/linux.md)** — Clang recommended.
- **[Windows](docs/getting-started/windows.md)** — Use `scripts\build_viper.cmd` instead. Clang-CL preferred. Some POSIX tests are skipped.

---

## 📖 Documentation

**Getting Started** — [Setup Guide](docs/getting-started.md) · [REPL Guide](docs/repl.md) · [Zia Tutorial](docs/zia-getting-started.md) · [The Viper Bible](docs/bible/README.md)

**Language References** — [Zia Reference](docs/zia-reference.md) · [BASIC Reference](docs/basic-reference.md) · [IL Guide](docs/il-guide.md) · [IL Quickstart](docs/il-quickstart.md)

**Runtime & APIs** — [Runtime Library](docs/viperlib/README.md) · [3D Graphics](docs/graphics3d-guide.md) · [Game Engine](docs/viperlib/game/README.md) · [GUI](docs/viperlib/gui/README.md)

**Internals** — [Architecture](docs/architecture.md) · [VM Design](docs/vm.md) · [Code Map](docs/codemap.md) · [Backend](docs/backend.md) · [IL Passes](docs/il-passes.md)

**For Contributors** — [Contributor Guide](docs/contributor-guide.md) · [Frontend How-To](docs/frontend-howto.md) · [Testing](docs/testing.md)

> Browse the full **[docs/](docs/)** hierarchy for 210+ documents.

---

## Contributing

Viper is in early development and the architecture is stabilizing. We welcome:

- Bug reports and issues
- Small fixes and documentation improvements
- Feedback and suggestions

We are not currently seeking large feature PRs while the design solidifies. Feel free to fork for broader experimentation.

---

## License

Viper is licensed under the **GNU General Public License v3.0** (GPL-3.0-only).

See [LICENSE](LICENSE) for the full text.
