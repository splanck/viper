---
status: active
audience: public
last-verified: 2026-01-15
---

# Getting Started with Viper

Welcome to Viper! This guide will help you build and run the Viper compiler toolchain.

---

## Prerequisites

Before you begin, ensure you have:

- **CMake** ≥ 3.20
- **C++20 Compiler**: GCC, Clang, or MSVC
- **Ninja** (optional): For faster multi-config builds
- **Python 3.x** (optional): For helper scripts in `scripts/`

---

## Building Viper

### 1. Configure the Build

Use an out-of-source build directory for clean rebuilds and configuration switching:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

**Build types:**

- `Debug` — Full debug symbols, no optimization
- `Release` — Optimized, minimal debug info
- `RelWithDebInfo` — Optimized with debug symbols (recommended)

### 2. Compile

```sh
cmake --build build -j
```

The `-j` flag enables parallel compilation for faster builds.

### 3. Optional: Install Locally

For packaging or downstream testing, install into a local prefix:

```sh
cmake --install build --prefix ./_install
```

---

## Verify the Installation

After building, confirm the primary tools work correctly:

```sh
./build/src/tools/vbasic/vbasic --help
./build/src/tools/zia/zia --help
./build/src/tools/ilrun/ilrun --help
./build/src/tools/il-verify/il-verify --help
```

Each tool should display its help message without errors.

---

## Quick Start: Run Your First Program

### BASIC

```sh
./build/src/tools/vbasic/vbasic examples/basic/ex1_hello_cond.bas
```

**Expected output:**

```
Hello, World!
Condition is true
```

### Zia

Create a file `hello.zia`:

```viper
module Hello;

func start() {
    Viper.Terminal.Say("Hello, World!");
}
```

Run it:

```sh
./build/src/tools/zia/zia hello.zia
```

**Expected output:**

```
Hello, World!
```

---

## Working with IL Programs

You can inspect the generated IL or run IL programs directly:

```sh
# Show generated IL from BASIC
./build/src/tools/vbasic/vbasic examples/basic/ex1_hello_cond.bas --emit-il

# Show generated IL from Zia
./build/src/tools/zia/zia hello.zia --emit-il

# Save IL to a file
./build/src/tools/vbasic/vbasic examples/basic/ex1_hello_cond.bas -o hello.il

# Run the IL program directly
./build/src/tools/ilrun/ilrun hello.il
```

For more examples, see the **[BASIC Tutorial](basic-language.md)**,
**[Zia Tutorial](zia-getting-started.md)**, and the `examples/` and `demos/` directories.

---

## Command Reference

### User-Facing Tools (Simplified CLI)

| Tool        | Purpose                       | Example                  |
|-------------|-------------------------------|--------------------------|
| `vbasic`    | Run/compile BASIC programs    | `vbasic script.bas`      |
| `zia`       | Run/compile Zia programs    | `zia program.zia`      |
| `ilrun`     | Execute IL programs           | `ilrun program.il`       |
| `il-verify` | Verify IL correctness         | `il-verify program.il`   |
| `il-dis`    | Disassemble IL                | `il-dis program.il`      |

### Advanced Tools

| Tool             | Purpose                     | Example                             |
|------------------|-----------------------------|-------------------------------------|
| `viper`          | Unified compiler (advanced) | `viper front basic -run script.bas` |
| `basic-ast-dump` | Dump BASIC AST              | `basic-ast-dump script.bas`       |
| `basic-lex-dump` | Dump BASIC tokens           | `basic-lex-dump script.bas`       |

> **Note:** The simplified tools (`vbasic`, `zia`, `ilrun`) are recommended for everyday use. The unified `viper`
command provides access to advanced features.

---

## Key Concepts

### Unified Error Model

All frontends, IL, and the VM share a consistent error and trap model. Diagnostics remain uniform regardless of entry
point.

> **Learn more:** See `devdocs/specs/errors.md` for trap kinds, handler semantics, and BASIC `ON ERROR` lowering rules.

### Deterministic Numerics

Viper guarantees consistent numeric behavior across all platforms and execution modes:

- **Overflow checking**: Integer arithmetic traps on overflow instead of wrapping
- **Division operators**: `/` performs floating-point division; `\` truncates toward zero
- **Modulo**: `MOD` preserves the dividend's sign
- **Rounding**: All rounding uses banker's rounding (ties-to-even)
- **Conversions**: Casts use checked variants that trap when values are out of range
- **String conversions**: `VAL` and `STR$` guarantee round-trip consistency

> **Learn more:** See `devdocs/specs/numerics.md` for complete numeric semantics.

---

## What to Read Next

**Language Documentation:**

- **[BASIC Tutorial](basic-language.md)** — Learn Viper BASIC by example
- **[BASIC Reference](basic-reference.md)** — Complete BASIC language reference
- **[Zia Tutorial](zia-getting-started.md)** — Learn Zia by example
- **[Zia Reference](zia-reference.md)** — Complete Zia language reference
- **[IL Guide](il-guide.md)** — Comprehensive IL documentation

**Implementation Guides:**

- **[Frontend How-To](frontend-howto.md)** — Build your own language frontend

**Developer Documentation** (in `/devdocs`):

- `architecture.md` — System architecture overview
- `runtime-vm.md` — VM and runtime internals
- `contributor-guide.md` — Contribution guidelines
- `tools.md` — CLI tools reference
