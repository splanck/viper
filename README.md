# Viper

**Viper** is an IL-first compiler toolchain and virtual machine for building platform-native applications. Programs are compiled through a strongly typed, SSA-inspired intermediate language (**Viper IL**) that can be executed by the VM or compiled directly to native machine code.

**Zia** is Viper's flagship language — a modern, statically typed language with entities, generics, modules, and pattern matching, designed for building real applications on the Viper platform. A BASIC frontend is also included for educational use and rapid prototyping.

> **Status:** Early development. APIs, IL, and tooling change frequently. Not production-ready.

---

## Download

**Latest Release:** [v0.2.0-dev](https://github.com/splanck/viper/releases/tag/v0.2.0-dev) (1/15/2026)

- [Source (tar.gz)](https://github.com/splanck/viper/archive/refs/tags/v0.2.0-dev.tar.gz)
- [Source (zip)](https://github.com/splanck/viper/archive/refs/tags/v0.2.0-dev.zip)
- [Release Notes](docs/release_notes/Viper_Release_Notes_0_2_0.md)

Or clone the repository:

```bash
git clone https://github.com/splanck/viper.git
cd viper
```

---

## Quickstart

Build and test:

```bash
./scripts/build_viper.sh
```

Create a new project:

```bash
viper init my-app          # Zia project (default)
viper init my-app --lang basic  # BASIC project
viper run my-app
```

Run a Zia program:

```bash
./build/src/tools/viper/viper run demos/zia/paint/main.zia
```

Run a project directory (auto-discovers language and entry point):

```bash
./build/src/tools/viper/viper run demos/zia/pacman/
```

Run an IL program directly:

```bash
./build/src/tools/ilrun/ilrun examples/il/ex1_hello_cond.il
```

---

## What is Viper?

Viper is a compiler infrastructure with several components:

| Component | Description |
|-----------|-------------|
| **Zia** | Modern, statically typed language with entities, generics, and modules |
| **IL** | Typed, SSA-based intermediate representation |
| **VM** | Bytecode interpreter with pluggable dispatch strategies |
| **Backends** | Native code generators (AArch64, x86-64) |
| **Runtime** | Portable C libraries for core types, collections, I/O, text, math, graphics, audio, GUI, input, networking, system, diagnostics, utilities, crypto, time, threading |
| **Tools** | Compiler drivers, verifier, disassembler |
| **BASIC** | Educational frontend for rapid prototyping |

### Why Viper?

- **Platform-native**: Zia compiles to native machine code via Viper IL — no VM required for production
- **IL-centric**: A readable, typed IR makes semantics explicit and frontends interchangeable
- **Full runtime**: Rich standard library with graphics, networking, threading, GUI, and more
- **Composable**: Parser, IL builder, verifier, and VM work as standalone scriptable tools

---

## Project Status

Viper is in **early development**. All components are functional but incomplete:

| Component | Notes |
|-----------|-------|
| Zia Frontend | Flagship language with entities, generics, modules, imports; actively developed |
| Viper IL | Stable core; instruction set still expanding |
| Virtual Machine | Functional with multiple dispatch strategies |
| AArch64 Backend | Validated on Apple Silicon; actively developed |
| x86-64 Backend | Validated on Windows; System V and Windows x64 ABI support |
| Runtime Libraries | Comprehensive: core types, collections, I/O, text, math, graphics, audio, GUI, input, networking, system, diagnostics, utilities, crypto, time, threads |
| BASIC Frontend | Core language implemented; OOP features work but are evolving |
| IL Optimizer | Basic passes implemented; more planned |
| Debugger/IDE | Early work; not yet usable |

Expect breaking changes. The IL specification, APIs, and tool interfaces are not stable.

---

## Demos

Several demos showcase the platform's capabilities. All demos can be compiled to native binaries using the demo build script:

```bash
# macOS / Linux (ARM64)
./scripts/build_demos.sh          # Build all demos as native ARM64 binaries
./scripts/build_demos.sh --clean  # Clean and rebuild

# Windows (x86-64)
scripts\build_demos.cmd           # Build all demos as native x86-64 binaries
scripts\build_demos.cmd --clean   # Clean and rebuild
```

Native binaries are output to `demos/bin/`. See the **[demos/](demos/README.md)** directory for more details.

### Zia Demos

| Demo | Description |
|------|-------------|
| `demos/zia/paint` | Full-featured paint application with drawing tools and color picker |
| `demos/zia/pacman` | Pac-Man clone with ghost AI, animations, and tile-based rendering |
| `demos/zia/sqldb` | SQL database engine with REPL, query execution, and persistent storage |

### BASIC Demos

| Demo | Description |
|------|-------------|
| `demos/basic/chess` | Console chess with AI opponent |
| `demos/basic/vtris` | Tetris clone with high scores |
| `demos/basic/frogger` | Frogger clone (console) |
| `demos/basic/centipede` | Classic arcade game with OOP |
| `demos/basic/pacman` | Pac-Man clone with ghost AI |

Run demos via the VM or compile to native:

```bash
# Run in VM
./build/src/tools/viper/viper run demos/zia/paint/

# Compile to native binary
./build/src/tools/viper/viper build demos/zia/paint/ -o paint
./paint
```

---

## Architecture

```
┌─────────────────────────────────────────┐
│           Source Languages              │
│           (Zia, BASIC, ...)             │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│     Frontend (Parser + Semantics +      │
│              Lowering)                  │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│         Viper IL (Typed SSA)            │
│        + Verifier + Optimizer           │
└─────────┬───────────────┬───────────────┘
          │               │
          ▼               ▼
┌──────────────┐   ┌──────────────────────┐
│   Virtual    │   │   Native Backend     │
│   Machine    │   │  (AArch64, x86-64)   │
└──────────────┘   └──────────────────────┘
        │                   │
        └─────────┬─────────┘
                  ▼
┌─────────────────────────────────────────┐
│           Viper Runtime                 │
│ (Collections, I/O, Text, Math, Graphics,│
│  Audio, GUI, Input, Network, Threads)   │
└─────────────────────────────────────────┘
```

---

## IL at a Glance

Frontends lower to a typed IL that is compact, explicit, and inspectable.

**Zia Source:**

```zia
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

```il
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

---

## Runtime Library

All frontends share the **Viper Runtime**, providing a growing set of modules:

| Module | Classes | Description |
|--------|---------|-------------|
| **Collections** | `Bag`, `Bytes`, `Heap`, `List`, `Map`, `Queue`, `Ring`, `Seq`, `Stack`, `TreeMap` | Data structures for any use case |
| **Core** | `Box`, `Object`, `String` | Base types and string operations |
| **Crypto** | `Hash`, `KeyDerive`, `Rand` | CRC32, MD5, SHA1, SHA256, PBKDF2, secure RNG |
| **Diagnostics** | `Assert`, `Trap` | Debugging and assertions |
| **Graphics** | `Camera`, `Canvas`, `Color`, `Pixels`, `Sprite`, `Tilemap` | 2D graphics, sprites, tilemaps, cameras |
| **GUI** | `App`, `Button`, `Checkbox`, `Label`, `Slider`, `TextInput`, etc. | Cross-platform GUI widgets and layouts |
| **I/O** | `Archive`, `BinFile`, `Compress`, `Dir`, `File`, `LineReader`, `LineWriter`, `MemStream`, `Path`, `Watcher` | File system access and streaming |
| **Input** | `Keyboard`, `Mouse`, `Pad` | Input devices for games and interactive apps |
| **Math** | `Bits`, `Math`, `Random`, `Vec2`, `Vec3` | Mathematical functions and vectors |
| **Network** | `Dns`, `Http`, `HttpReq`, `HttpRes`, `Tcp`, `TcpServer`, `Udp`, `Url` | Networking and sockets |
| **Sound** | `Audio`, `Music`, `Sound`, `Voice` | Audio playback and sound effects |
| **System** | `Environment`, `Exec`, `Machine`, `Terminal` | System interaction and console I/O |
| **Text** | `Codec`, `Csv`, `Uuid`, `Pattern`, `StringBuilder`, `Template` | String building and text encoding |
| **Threads** | `Barrier`, `Gate`, `Monitor`, `RwLock`, `SafeI64`, `Thread` | Concurrent programming primitives |
| **Time** | `Clock`, `Countdown`, `DateTime`, `Stopwatch` | Time utilities and measurement |
| **Utilities** | `Convert`, `Fmt`, `Log`, `Parse` | Conversion, formatting, parsing, logging |

See the **[Runtime Library Reference](docs/viperlib/README.md)** for complete API documentation.

---

## Tools

**Primary tools:**

| Tool | Purpose |
|------|---------|
| `viper` | Unified compiler driver — run, build, and compile projects |
| `zia` | Run or compile Zia programs |
| `vbasic` | Run or compile BASIC programs |
| `ilrun` | Execute IL programs |
| `il-verify` | Validate IL with detailed diagnostics |
| `il-dis` | Disassemble IL for inspection |

**Examples:**

```bash
# Run a Zia source file
./build/src/tools/viper/viper run program.zia

# Run a project directory (discovers files and entry point)
./build/src/tools/viper/viper run demos/zia/pacman/

# Compile to IL
./build/src/tools/viper/viper build program.zia -o output.il

# Run with standalone tools
./build/src/tools/zia/zia program.zia
./build/src/tools/ilrun/ilrun program.il

# Verify IL
./build/src/tools/il-verify/il-verify program.il
```

**Native compilation (experimental):**

```bash
./build/src/tools/viper/viper build program.zia -o program.il
./build/src/tools/viper/viper codegen arm64 program.il -o program
```

---

## VM Dispatch Strategies

The VM supports three dispatch strategies:

| Strategy | Description | Portability |
|----------|-------------|-------------|
| `switch` | Switch-based dispatch | All compilers |
| `table` | Function-pointer dispatch | All compilers |
| `threaded` | Direct-threaded (labels-as-values) | GCC/Clang only |

Set at runtime:

```bash
VIPER_DISPATCH=threaded ./build/src/tools/ilrun/ilrun program.il
```

Build with threaded dispatch enabled by default:

```bash
cmake -S . -B build -DVIPER_VM_THREADED=ON
```

---

## Building

### Requirements

- CMake 3.20+
- C++20 compiler (Clang recommended, GCC 11+, or MSVC)

### Build Steps

```bash
./scripts/build_viper.sh
```

This configures, builds, tests, and installs Viper in one step.

### Platform Notes

- **macOS**: Use Apple Clang. ARM64 tests skip x86-64-specific checks automatically.
- **Linux**: Clang recommended.
- **Windows**: Use `scripts/build_viper.cmd` instead. Clang-CL preferred. Some POSIX tests are skipped.

---

## Documentation

| Document | Description |
|----------|-------------|
| [Getting Started](docs/getting-started.md) | Build and run your first program |
| [Zia Getting Started](docs/zia-getting-started.md) | Learn Zia by example |
| [Zia Reference](docs/zia-reference.md) | Complete Zia language specification |
| [Runtime Library](docs/viperlib/README.md) | Viper.* classes, methods, and properties |
| [IL Guide](docs/il-guide.md) | IL specification and examples |
| [IL Quickstart](docs/il-quickstart.md) | Fast introduction to Viper IL |
| [VM Architecture](docs/vm.md) | VM design and internals |
| [BASIC Tutorial](docs/basic-language.md) | Learn Viper BASIC by example |
| [BASIC Reference](docs/basic-reference.md) | Complete BASIC language specification |
| [Frontend How-To](docs/frontend-howto.md) | Build your own language frontend |

Developer documentation is in `docs/devdocs/`.

---

## Contributing

Viper is in early development and the architecture is stabilizing. We welcome:

- Bug reports and issues
- Small fixes and documentation improvements
- Feedback and suggestions

We are not currently seeking large feature PRs while the design solidifies. Feel free to fork for broader experimentation.

---

## Related Project: ViperDOS

This repository also contains [ViperDOS](viperdos/README.md), a capability-based
microkernel operating system for AArch64. ViperDOS is a separate project and
does not depend on the Viper compiler.

See [viperdos/README.md](viperdos/README.md) for documentation.

---

## License

Viper is licensed under the **GNU General Public License v3.0** (GPL-3.0-only).

See [LICENSE](LICENSE) for the full text.
