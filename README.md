<p align="center">
  <img src="misc/images/viperlogo2.png" alt="Viper" width="160">
</p>

<h1 align="center">Viper</h1>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPL v3"></a>
  <img src="https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-brightgreen" alt="Platform">
</p>

**Viper** is a from-scratch, IL-first compiler toolchain and virtual machine for building platform-native applications and games. Source languages lower to a typed intermediate language, **[Viper IL](docs/il-guide.md)**, which can run on the [VM](docs/vm.md), feed the optimizer, or compile to native code through the built-in backends.

**[Zia](docs/zia-reference.md)** is the flagship language: a modern, statically typed language with classes, generics, enums, lambdas, modules, pattern matching, and direct access to the Viper runtime. A **[BASIC](docs/basic-reference.md)** frontend is included for education, compatibility experiments, and quick prototypes.

> **Status:** Pre-alpha, active development. The current source tree is `v0.2.99`; the IL reference is `0.3.0`. APIs, diagnostics, IL rules, and tooling are still evolving.

---

## Download

**Latest tagged release:** [v0.2.7-dev](https://github.com/splanck/viper/releases/tag/v0.2.7-dev) (2026-06-30)

- [Source (tar.gz)](https://github.com/splanck/viper/archive/refs/tags/v0.2.7-dev.tar.gz)
- [Source (zip)](https://github.com/splanck/viper/archive/refs/tags/v0.2.7-dev.zip)
- [Release notes](docs/release_notes/Viper_Release_Notes_0_2_7.md)

**In development:** `v0.2.99` on `master`. See the [draft v0.2.99 release notes](docs/release_notes/Viper_Release_Notes_0_2_99.md).

```sh
git clone https://github.com/splanck/viper.git
cd viper
```

---

## Quickstart

Build, test, and install the toolchain with the platform scripts:

```sh
# macOS
./scripts/build_viper_mac.sh

# Linux
./scripts/build_viper_linux.sh

# Windows
.\scripts\build_viper_win.cmd
```

Verify the build:

```sh
viper --version
```

Create and run a project:

```sh
viper init my-app              # Zia project (default)
viper init my-app --lang basic # BASIC project
viper run my-app
```

Try the [REPL](docs/repl.md):

```sh
viper repl
```

```text
zia> 2 + 3 * 4
14
zia> Say("Hello from Viper")
Hello from Viper
```

See the [Getting Started Guide](docs/getting-started.md) for platform-specific setup, build directory layout, and troubleshooting.

---

## Components

| Component | Description |
|-----------|-------------|
| **[Zia](docs/zia-reference.md)** | Statically typed application language with classes, generics, modules, lambdas, enums, and pattern matching |
| **[BASIC](docs/basic-reference.md)** | Educational and prototyping frontend that lowers to the same IL |
| **[Viper IL](docs/il-guide.md)** | Typed, block-structured SSA-style IR with a normative `0.3.0` reference |
| **[Optimizer](docs/il-passes.md)** | Registered O1/O2 pipelines covering SSA promotion, SCCP, GVN, LICM, loop cleanup, inlining, devirtualization, runtime fast paths, and cleanup passes |
| **[VM](docs/vm.md)** | IL execution engine for fast bring-up, tests, debugging, and step-budgeted runs |
| **[Native backends](docs/backend.md)** | AArch64 and x86-64 code generators with backend optimization, register allocation, assembly emission, and executable output |
| **[Assembler](docs/codegen/native-assembler.md) / [Linker](docs/codegen/native-linker.md)** | In-tree ELF, Mach-O, and PE object/link support with DWARF and platform packaging integration |
| **[Runtime](docs/viperlib/README.md)** | Shared standard library for collections, graphics, 3D, GUI, games, networking, crypto, text, threads, localization, and more |
| **[Language servers](docs/zia-server.md)** | Zia and BASIC servers with LSP and MCP modes for editors and AI coding tools |
| **[Tools](docs/tools.md)** | Unified `viper` driver plus `zia`, `vbasic`, `ilrun`, `il-verify`, `il-dis`, REPL, package, installer, and benchmark commands |

### Why Viper?

- **IL thin waist**: Zia, BASIC, and future frontends share one typed IR, verifier, optimizer, VM, and native backend path.
- **Self-contained native toolchain**: Viper includes its own runtime, assembler, linker, object writers, package generation, and install packaging paths.
- **Machine-readable tooling**: `viper check`, `viper eval`, `viper explain`, `--dump-runtime-api`, and `--dump-opcodes` expose JSON-friendly surfaces for editors, scripts, and AI agents.
- **Cross-platform by design**: macOS, Linux, and Windows are first-class targets; platform checks are centralized in adapter layers.
- **Runtime-first apps and games**: The standard library includes 2D/3D graphics, GUI widgets, game systems, audio, networking, localization, threading, and structured data APIs.

---

## Examples

The [examples](examples/README.md) tree includes applications, games, 3D scenes, API audits, language samples, IL programs, and C++ embedding demos.

| Demo | Description |
|------|-------------|
| [ViperSQL](examples/apps/vipersql/) | PostgreSQL-compatible SQL database server and client |
| [Paint](examples/apps/paint/) | Drawing app with tools, layers, file dialogs, zoom, and undo/redo |
| [WebServer](examples/apps/webserver/) | Multi-threaded HTTP server demo |
| [Chess](examples/games/chess/) | GUI chess with alpha-beta AI and drag-and-drop play |
| [Crackman](examples/games/crackman/) | Maze chase game with pathfinding and mode-driven AI |
| [XENOSCAPE](examples/games/xenoscape/) | Metroidvania-style sidescroller with abilities, enemies, levels, and saves |
| [3D Bowling](examples/games/3dbowling/) | Physics-driven 3D bowling with camera modes |
| [Ridgebound](examples/games/ridgebound/) | Open-world Game3D sample with terrain, water, skybox, PBR materials, beacon objectives, and post-FX |

```sh
viper run examples/games/chess/
viper build examples/apps/paint/ -o paint
./scripts/build_demos.sh
```

---

## Architecture

```text
+-------------------------+
|    Source Languages     |
|      Zia / BASIC        |
+-----------+-------------+
            |
            v
+-------------------------+
|  Parser / Sema / Lower  |
+-----------+-------------+
            |
            v
+-------------------------+
|       Viper IL          |
|  Verifier / Optimizer   |
+------+------------+-----+
       |            |
       v            v
+-------------+  +-----------------+
|     VM      |  | Native Backends |
|  Interpreter|  | AArch64/x86-64  |
+------+------+  +-------+---------+
       |                 |
       +--------+--------+
                v
+-------------------------+
|      Viper Runtime      |
| Collections / Graphics  |
| GUI / Game / Network    |
| Text / Threads / ...    |
+-------------------------+
```

See the [Architecture Overview](docs/architecture.md) and [Code Map](docs/codemap.md) for subsystem details.

---

## IL at a Glance

Frontends lower to typed [Viper IL](docs/il-guide.md) that is compact, explicit, and inspectable.

**Zia source:**

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

**Representative IL:**

```llvm
il 0.3.0
extern @Viper.Text.Fmt.Int(i64) -> str
extern @Viper.Terminal.Say(str) -> void
global const str @.L0 = "HELLO"
func @main() -> void {
entry_0:
  %t0 = iadd.ovf 2, 3
  %t1 = imul.ovf %t0, 2
  %t2 = const_str @.L0
  call @Viper.Terminal.Say(%t2)
  %t3 = call @Viper.Text.Fmt.Int(%t1)
  call @Viper.Terminal.Say(%t3)
  ret
}
```

Use `--dump-il`, `--dump-il-opt`, and `viper il-opt` to inspect the pipeline. The [IL Quickstart](docs/il-quickstart.md) is the practical introduction; [IL Guide](docs/il-guide.md) is the normative reference.

---

## Runtime Library

All frontends share the [Viper Runtime Library](docs/viperlib/README.md). The runtime surface is generated from the live registry and spans:

- Collections, core types, functional helpers, math, text, structured data, I/O, time, and utilities
- 2D graphics, 3D graphics, Game3D, GUI widgets, input, audio, and game systems
- Networking, crypto, diagnostics, memory controls, threading, system APIs, and localization

Authoritative runtime inventory:

```sh
viper --dump-runtime-api
```

Authoritative IL opcode inventory:

```sh
viper --dump-opcodes
```

---

## Tools

| Command                                | Purpose |
|----------------------------------------|---------|
| `viper run <file                       |dir>` | Build and run a source file, project directory, or manifest |
| `viper build <file                     |dir> -o <out>` | Build IL or a native executable |
| `viper check <file                     |dir> --diagnostic-format=json` | Type-check and verify without running; JSON diagnostics include ranges, notes, and fixits |
| `viper eval 'expr' --json --type --il` | Evaluate a Zia or BASIC snippet through the REPL pipeline |
| `viper explain <CODE> --json`          | Explain a diagnostic code from the central catalog |
| `viper repl [zia & basic]`             | Interactive REPL |
| `viper -run <file.il>`                 | Execute an IL module directly, with optional tracing and step limits |
| `viper package <dir>`                  | Package an application for macOS, Linux, Windows, or tarball output |
| `viper install-package`                | Package the Viper binary tools and ViperIDE into a platform installer |
| `zia` / `vbasic`                       | Standalone source compiler entry points |
| `zia-server` / `vbasic-server`         | Language servers with LSP and MCP modes |
| `ilrun`, `il-verify`, `il-dis`         | Direct IL execution, verification, and disassembly |
| `viper il-opt`                         | Run and inspect optimizer pipelines |
| `viper bench`                          | IL benchmark runner |

Common examples:

```sh
viper run program.zia
viper -run program.il --max-steps 100000
viper build project/ -o app
viper check project/ --diagnostic-format=json
viper eval '2 + 3 * 4' --json --type
viper explain V-ZIA-UNDEFINED --json
viper --dump-runtime-api
viper --dump-opcodes
```

See the [Tools Reference](docs/tools.md), [Debugging Guide](docs/debugging.md), and [MCP tool reference](docs/zia-server-mcp-tools.md) for full details.

---

## Building

### Requirements

- CMake 3.20+
- C++20 compiler: Apple Clang, Clang, GCC 11+, or MSVC

### Build Scripts

```sh
# macOS
./scripts/build_viper_mac.sh

# Linux
./scripts/build_viper_linux.sh

# Windows
.\scripts\build_viper_win.cmd
```

The scripts configure, build, test, and install Viper. The Unix wrappers delegate to `scripts/build_viper_unix.sh`.

Useful iteration knobs:

| Variable | Effect |
|----------|--------|
| `VIPER_SKIP_CLEAN=1` | Incremental rebuild |
| `VIPER_SKIP_TESTS=1` | Build only |
| `VIPER_TEST_LABEL=<label>` | Run one CTest label |
| `VIPER_CMAKE_GENERATOR=Ninja` | Select Ninja |

Targeted checks after a build:

```sh
ctest --test-dir build -L codegen --output-on-failure
ctest --test-dir build -R test_zia_lexer --output-on-failure
./scripts/example_smoke.sh --fast
```

Platform guides:

- [macOS](docs/getting-started/macos.md)
- [Linux](docs/getting-started/linux.md)
- [Windows](docs/getting-started/windows.md)

---

## Documentation

**Getting Started**: [Setup Guide](docs/getting-started.md), [REPL Guide](docs/repl.md), [Zia Tutorial](docs/zia-getting-started.md), [Viper Bible](docs/bible/README.md)

**Language References**: [Zia](docs/zia-reference.md), [BASIC](docs/basic-reference.md), [IL Guide](docs/il-guide.md), [IL Quickstart](docs/il-quickstart.md)

**Runtime & APIs**: [Runtime Library](docs/viperlib/README.md), [3D Graphics](docs/graphics3d-guide.md), [Game Engine](docs/gameengine/README.md), [GUI](docs/viperlib/gui/README.md)

**Internals**: [Architecture](docs/architecture.md), [VM](docs/vm.md), [Code Map](docs/codemap.md), [Backend](docs/backend.md), [IL Passes](docs/il-passes.md)

**Contributors**: [Contributor Guide](docs/contributor-guide.md), [Frontend How-To](docs/frontend-howto.md), [Testing](docs/testing.md), [Dependencies](docs/dependencies.md)

---

## Contributing

Viper is in active development and the architecture is still stabilizing. Small fixes, documentation improvements, bug reports, and focused tests are welcome.

Before proposing changes:

- Read the relevant spec or reference first; [IL Guide](docs/il-guide.md) is normative for IL.
- Keep the product dependency-free.
- Preserve macOS, Windows, and Linux behavior.
- Use the platform build scripts and keep tests green.
- Follow [Conventional Commits](https://www.conventionalcommits.org/) for commit messages.

See [Contributor Guide](docs/contributor-guide.md) and [Testing](docs/testing.md) for the full workflow.

---

## License

Viper is licensed under the **GNU General Public License v3.0** (GPL-3.0-only).

See [LICENSE](LICENSE) for the full text.
