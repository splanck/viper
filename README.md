# Viper

**Viper** is an IL-first compiler toolchain and virtual machine for exploring intermediate language design, multi-frontend architectures, and interpreter implementation techniques.

High-level frontends—like the included BASIC and ViperLang compilers—lower programs into a strongly typed, SSA-inspired intermediate language (**Viper IL**). The IL can be executed by the VM or compiled to native code.

> **Status:** Early development. APIs, IL, and tooling change frequently. Not production-ready.

---

## Download

**Latest Release:** [v0.1.3-dev](https://github.com/splanck/viper/releases/tag/v0.1.3-dev)

- [Source (tar.gz)](https://github.com/splanck/viper/archive/refs/tags/v0.1.3-dev.tar.gz)
- [Source (zip)](https://github.com/splanck/viper/archive/refs/tags/v0.1.3-dev.zip)
- [Release Notes](docs/release_notes/Viper_Release_Notes_0_1_3.md)

Or clone the repository:

```bash
git clone https://github.com/splanck/viper.git
cd viper
```

---

## Quickstart

Build and test:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run a BASIC program:

```bash
./build/src/tools/vbasic/vbasic examples/basic/ex1_hello_cond.bas
```

Run a ViperLang program:

```bash
./build/src/tools/viper/viper demos/viperlang/frogger/main.viper
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
| **IL** | Typed, SSA-based intermediate representation |
| **Frontends** | Language compilers: BASIC and ViperLang |
| **VM** | Bytecode interpreter with pluggable dispatch strategies |
| **Backends** | Native code generators (AArch64, x86-64) |
| **Runtime** | Portable C libraries for core types, collections, I/O, text, math, graphics, audio, GUI, input, networking, system, diagnostics, utilities, crypto, time, threading |
| **Tools** | Compiler drivers, verifier, disassembler |

### Why Viper?

- **IL-centric**: A readable, typed IR makes semantics explicit and frontends interchangeable
- **Human-scale**: The IL is designed to be read and edited—learn by inspecting output
- **Composable**: Parser, IL builder, verifier, and VM work as standalone scriptable tools
- **Educational**: Clear examples, golden tests, and a manageable codebase for experimentation

---

## Project Status

Viper is in **early development**. All components are functional but incomplete:

| Component | Notes |
|-----------|-------|
| BASIC Frontend | Core language implemented; OOP features work but are evolving |
| ViperLang Frontend | Core language with entities, generics, imports; actively developed |
| Viper IL | Stable core; instruction set still expanding |
| Virtual Machine | Functional with multiple dispatch strategies |
| AArch64 Backend | Validated on Apple Silicon; actively developed |
| x86-64 Backend | Validated on Windows; System V and Windows x64 ABI support |
| Runtime Libraries | Comprehensive: core types, collections, I/O, text, math, graphics, audio, GUI, input, networking, system, diagnostics, utilities, crypto, time, threads |
| IL Optimizer | Basic passes implemented; more planned |
| Debugger/IDE | Early work; not yet usable |

Expect breaking changes. The IL specification, APIs, and tool interfaces are not stable.

---

## Demos

Several demos showcase the platform's capabilities. See the **[demos/](demos/README.md)** directory for the full list.

### BASIC Demos

| Demo | Description |
|------|-------------|
| `demos/basic/frogger` | Frogger clone (console). Also runs natively on Apple Silicon. |
| `demos/basic/chess` | Console chess with AI opponent |
| `demos/basic/pacman` | Pac-Man clone with ghost AI |
| `demos/basic/centipede` | Classic arcade game with OOP |
| `demos/basic/monopoly` | Board game with 4-player AI |
| `demos/basic/vtris` | Tetris clone with high scores |
| `demos/basic/particles` | Graphics particle system using Canvas API |

### ViperLang Demos

| Demo | Description |
|------|-------------|
| `demos/viperlang/frogger` | Frogger with entity types and generics |
| `demos/viperlang/centipede` | Centipede arcade game clone |
| `demos/viperlang/ladders` | Donkey Kong-style platformer |
| `demos/viperlang/paint` | Full-featured paint application |
| `demos/viperlang/vedit` | Text editor with syntax highlighting |
| `demos/viperlang/graphics_show` | Graphics API showcase |
| `demos/viperlang/sql` | SQL query demonstration |
| `demos/viperlang/telnet` | Telnet client |

Run demos:

```bash
# BASIC
./build/src/tools/vbasic/vbasic demos/basic/frogger/frogger.bas

# ViperLang
./build/src/tools/viper/viper demos/viperlang/frogger/main.viper
```

---

## Architecture

```
┌─────────────────────────────────────────┐
│           Source Language               │
│         (BASIC, ViperLang)              │
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

**BASIC Source:**

```basic
10 LET X = 2 + 3
20 LET Y = X * 2
30 PRINT "HELLO"
40 PRINT Y
50 END
```

**Viper IL Output:**

```il
il 0.2
extern @Viper.Console.PrintStr(str) -> void
extern @Viper.Console.PrintI64(i64) -> void
global const str @.NL = "\n"
global const str @.HELLO = "HELLO"

func @main() -> i64 {
entry:
  %x = add 2, 3
  %y = mul %x, 2
  call @Viper.Console.PrintStr(const_str @.HELLO)
  call @Viper.Console.PrintStr(const_str @.NL)
  call @Viper.Console.PrintI64(%y)
  call @Viper.Console.PrintStr(const_str @.NL)
  ret 0
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
| **Text** | `Codec`, `Csv`, `Guid`, `Pattern`, `StringBuilder`, `Template` | String building and text encoding |
| **Threads** | `Barrier`, `Gate`, `Monitor`, `RwLock`, `SafeI64`, `Thread` | Concurrent programming primitives |
| **Time** | `Clock`, `Countdown`, `DateTime`, `Stopwatch` | Time utilities and measurement |
| **Utilities** | `Convert`, `Fmt`, `Log`, `Parse` | Conversion, formatting, parsing, logging |

See the **[Runtime Library Reference](docs/viperlib/README.md)** for complete API documentation.

---

## Tools

**Primary tools:**

| Tool | Purpose |
|------|---------|
| `vbasic` | Run or compile BASIC programs |
| `viper` | Run or compile ViperLang programs |
| `ilrun` | Execute IL programs |
| `il-verify` | Validate IL with detailed diagnostics |
| `il-dis` | Disassemble IL for inspection |

**Examples:**

```bash
# Run BASIC
./build/src/tools/vbasic/vbasic program.bas

# Run ViperLang
./build/src/tools/viper/viper program.viper

# Emit IL from any frontend
./build/src/tools/vbasic/vbasic program.bas --emit-il
./build/src/tools/viper/viper program.viper --emit-il

# Run IL
./build/src/tools/ilrun/ilrun program.il

# Verify IL
./build/src/tools/il-verify/il-verify program.il
```

**Advanced tool:**

`ilc` is a unified compiler driver for advanced workflows:

```bash
# Compile BASIC to native executable (experimental)
./build/src/tools/ilc/ilc front basic -emit-il program.bas > program.il
./build/src/tools/ilc/ilc codegen arm64 program.il -o program

# Compile ViperLang to native executable
./build/src/tools/ilc/ilc front viperlang -emit-il program.viper > program.il
./build/src/tools/ilc/ilc codegen arm64 program.il -o program
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
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Install (Optional)

```bash
sudo cmake --install build --prefix /usr/local
```

Installs: `vbasic`, `vpascal`, `viper`, `ilrun`, `ilc`, `il-verify`, `il-dis`

### Platform Notes

- **macOS**: Use Apple Clang. ARM64 tests skip x86-64-specific checks automatically.
- **Linux**: Clang recommended. Force with `CC=clang CXX=clang++ cmake -S . -B build`
- **Windows**: Clang-CL preferred. Some POSIX tests are skipped.

---

## Documentation

| Document | Description |
|----------|-------------|
| [Getting Started](docs/getting-started.md) | Build and run your first program |
| [BASIC Tutorial](docs/basic-language.md) | Learn Viper BASIC by example |
| [BASIC Reference](docs/basic-reference.md) | Complete BASIC language specification |
| [ViperLang Getting Started](docs/viperlang-getting-started.md) | Learn ViperLang by example |
| [ViperLang Reference](docs/viperlang-reference.md) | Complete ViperLang language specification |
| [Runtime Library](docs/viperlib/README.md) | Viper.* classes, methods, and properties |
| [IL Guide](docs/il-guide.md) | IL specification and examples |
| [IL Quickstart](docs/il-quickstart.md) | Fast introduction to Viper IL |
| [VM Architecture](docs/vm.md) | VM design and internals |
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

## Related Project: ViperOS

This repository also contains [ViperOS](viperos/README.md), a capability-based
microkernel operating system for AArch64. ViperOS is a separate project and
does not depend on the Viper compiler.

See [viperos/README.md](viperos/README.md) for documentation.

---

## License

Viper is licensed under the **GNU General Public License v3.0** (GPL-3.0-only).

See [LICENSE](LICENSE) for the full text.
