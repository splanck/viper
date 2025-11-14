# Viper

**Viper** is an **IL-first compiler toolchain and virtual machine** for exploring intermediate language design, multi-frontend architectures, and interpreter micro-architectures.

High-level frontends—like the included BASIC compiler—lower programs into a strongly typed, SSA-inspired intermediate language (**Viper IL**). The IL can be executed by the VM or compiled to native code via the x86-64 backend.

---

## What is Viper?

Viper is a complete compiler infrastructure with multiple components:

- **IL (Intermediate Language)**: Typed, SSA-based IR that serves as the universal compilation target
- **Frontends**: Language-specific compilers that lower to IL (BASIC included, more planned)
- **VM**: Bytecode interpreter with pluggable dispatch strategies (switch, table, threaded)
- **Backend**: Native code generator targeting x86-64 (Phase A complete)
- **Runtime**: Portable C libraries for strings, math, file I/O, and memory management
- **Tooling**: Compiler driver, verifier, disassembler, and debugger integration

---

## Why Viper?

- **IL at the center**: A single, readable, typed IR makes semantics explicit and frontends interchangeable
- **Human-scale design**: The IL is meant to be *read and edited*—you can learn by inspecting disassembly
- **Composable toolchain**: Parsers, IL builder, verifier, and VM all exist as standalone tools you can script
- **Performance playground**: Multiple dispatch strategies let you *feel* interpreter trade-offs
- **Teaching & research friendly**: Clear examples, golden tests, and a small surface area encourage experimentation

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
./build/src/tools/ilc/ilc front basic -run examples/basic/ex1_hello_cond.bas
```

Run an IL program:

```bash
./build/src/tools/ilc/ilc -run examples/il/ex1_hello_cond.il
```

Compile to native code (x86-64):

```bash
./build/src/tools/ilc/ilc front basic examples/basic/ex1_hello_cond.bas -o hello
./hello
```

---

## Features

### Implemented

**Languages:**
- **BASIC Frontend**: Complete parser, semantic analysis, OOP features (classes, methods, inheritance), and runtime integration

**Core Infrastructure:**
- **Viper IL**: Stable, typed, SSA-style IR with comprehensive verifier
- **Virtual Machine**: Configurable dispatch strategies:
  - `switch` — Classic switch-based jump table (portable)
  - `table` — Function-pointer dispatch (portable)
  - `threaded` — Direct-threaded labels-as-values (GCC/Clang only)
- **x86-64 Backend**: Native code generation with linear-scan register allocation, instruction selection, and frame lowering
- **Runtime Libraries**: Portable C implementations for strings, math, file I/O, and memory management

**Tooling:**
- **`ilc`**: Unified compiler driver (compile and run BASIC or IL programs)
- **`il-verify`**: Standalone IR verifier with detailed diagnostics
- **`il-dis`**: IL disassembler for inspection
- **`basic-ast-dump`**: BASIC AST visualizer

**Quality Assurance:**
- Extensive golden test suite across all layers
- Deterministic numeric semantics (overflow checking, banker's rounding)
- Unified error model across frontends, IL, and VM

### In Progress

- IL optimization passes (mem2reg, SimplifyCFG, LICM, SCCP)
- Advanced register allocation (graph coloring)
- Debugger and IDE integration
- Additional language frontends

---

## IL at a Glance

Viper's core philosophy: **frontends lower to a typed IL that is compact, explicit, and easy to inspect.**

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
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
global const str @.NL = "\n"
global const str @.HELLO = "HELLO"

func @main() -> i64 {
entry:
  %x = add 2, 3
  %y = mul %x, 2
  call @rt_print_str(const_str @.HELLO)
  call @rt_print_str(const_str @.NL)
  call @rt_print_i64(%y)
  call @rt_print_str(const_str @.NL)
  ret 0
}
```

---

## Architecture Overview

```
┌─────────────────────────────────────────┐
│           Source Language                │
│        (BASIC, others planned)           │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│          Frontend (Parser +              │
│       Semantic Analysis + Lowering)      │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│          Viper IL (Typed SSA)            │
│         + Verifier + Optimizer           │
└─────────┬───────────────┬───────────────┘
          │               │
          ▼               ▼
┌──────────────┐   ┌──────────────────────┐
│   Virtual    │   │  Backend (x86-64)    │
│   Machine    │   │   Native Codegen     │
└──────────────┘   └──────────────────────┘
```

- **Frontends** lower source languages to a common, typed IL
- **Verifier** enforces type safety, control-flow correctness, and SSA properties
- **VM** executes IL with configurable dispatch strategies
- **Backend** compiles IL to native machine code (x86-64 with System V ABI)

---

## Tools

- **`ilc`** — Unified compiler driver
  - Compile and run BASIC: `ilc front basic -run program.bas`
  - Run IL directly: `ilc -run program.il`
  - Compile to native: `ilc front basic program.bas -o executable`
  - Generate assembly: `ilc front basic program.bas --emit-asm -o program.s`

- **`il-verify`** — Standalone IR verifier
  - Verify IL: `il-verify program.il`
  - Outputs detailed diagnostics with function/block context

- **`il-dis`** — IL disassembler
  - Disassemble IL: `il-dis program.il`

---

## Documentation

**Getting Started:**
- [Getting Started](docs/getting-started.md) — Build, install, and run your first program

**Language References:**
- [BASIC Tutorial](docs/basic-language.md) — Learn Viper BASIC by example
- [BASIC Reference](docs/basic-reference.md) — Complete language specification
- [IL Guide](docs/il-guide.md) — Comprehensive IL specification and examples
- [IL Quickstart](docs/il-quickstart.md) — Fast introduction to Viper IL
- [IL Reference](docs/il-reference.md) — Complete IL instruction catalog

**Implementation Guides:**
- [Frontend How-To](docs/frontend-howto.md) — Build your own language frontend
- [VM Architecture](docs/vm.md) — VM design, execution model, and internals
- [Backend Guide](docs/backend.md) — x86-64 code generation architecture

**Developer Resources:**
- See `/devdocs` for architecture diagrams, code maps, and contributor guides

---

## Building & Installation

### Requirements

- **CMake** 3.20 or later
- **C++20 compiler**: Clang (recommended), GCC 11+, or MSVC
- **Ninja** (optional): For faster multi-config builds
- **Python 3.x** (optional): For helper scripts

### Build

```bash
# Configure
cmake -S . -B build

# Optional: Enable direct-threaded VM dispatch (GCC/Clang only)
cmake -S . -B build -DVIPER_VM_THREADED=ON

# Build
cmake --build build -j

# Test
ctest --test-dir build --output-on-failure
```

### Install

```bash
# Install to /usr/local (macOS/Linux)
sudo cmake --install build --prefix /usr/local

# Or install to a custom prefix
cmake --install build --prefix "$HOME/.local"
```

**What gets installed:**
- Binaries: `ilc`, `il-verify`, `il-dis` → `${prefix}/bin`
- Headers: Public API headers → `${prefix}/include/viper`
- Man pages: → `${prefix}/share/man/man1`

### Uninstall

```bash
cmake --build build --target uninstall
```

### Platform Notes

**macOS:**
- Use Apple Clang (Xcode or Command Line Tools)
- On Apple Silicon (arm64), x86-64 tests are skipped automatically

**Linux:**
- Clang recommended; GCC 11+ supported
- Force compiler: `CC=clang CXX=clang++ cmake -S . -B build`

**Windows:**
- Clang-CL preferred; MSVC may work but is not primary configuration
- Some POSIX-specific tests are skipped

---

## VM Dispatch Strategies

The VM supports three dispatch strategies optimized for different use cases:

| Strategy    | Description | Portability | Performance |
|-------------|-------------|-------------|-------------|
| `switch`    | Switch-based jump table | All compilers | Good |
| `table`     | Function-pointer dispatch | All compilers | Good |
| `threaded`  | Direct-threaded (labels-as-values) | GCC/Clang only | Best |

**Configure at runtime** with the `VIPER_DISPATCH` environment variable:

```bash
VIPER_DISPATCH=threaded ./build/src/tools/ilc/ilc -run program.il
```

If built with `-DVIPER_VM_THREADED=ON`, the VM defaults to `threaded` when available.

**Performance note:** Direct-threaded dispatch reduces branch mispredictions in tight interpreter loops. Workloads dominated by I/O or native library calls see minimal benefit.

---

## Extending Viper

Adding a new language frontend:

1. **Parse** your language into an AST
2. **Lower** to Viper IL using the IL builder API
3. **Verify** with `il-verify` to catch errors
4. **Execute** via the VM or compile to native code

See [Frontend How-To](docs/frontend-howto.md) for a complete implementation guide.

---

## Project Status

| Component | Status |
|-----------|--------|
| BASIC Frontend + OOP | Active development |
| Virtual Machine | Active development |
| x86-64 Backend | Phase A complete |
| Runtime Libraries | Active development |
| IL Verifier | Active development |
| IL Optimizer | In progress |
| Debugger/IDE | Planned |
| TUI Subsystem | Experimental |

---

## Contributing

Viper is evolving quickly and the architecture is still stabilizing. We welcome:

- Bug reports and issue filing
- Small fixes and documentation improvements
- Feedback and suggestions

We are **not currently seeking large feature PRs** while the core design solidifies. If you want to experiment more broadly, feel free to fork—Viper is MIT-licensed.

---

## License

MIT License

Copyright (c) 2025 - Viper Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

See [LICENSE](LICENSE) for full text.
