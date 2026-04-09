---
status: active
audience: public
last-verified: 2026-04-05
---

# Getting Started with Viper

Welcome to Viper! This guide will help you build and run the Viper compiler toolchain.

---

## Platform Guides

For detailed, platform-specific installation instructions (prerequisites, exact install commands, and troubleshooting):

- **[macOS](getting-started/macos.md)**
- **[Linux](getting-started/linux.md)**
- **[Windows](getting-started/windows.md)**

---

## Prerequisites

Before you begin, ensure you have:

- **CMake** ≥ 3.20
- **C++20 Compiler**: GCC, Clang, or MSVC
- **Ninja** (optional): For faster multi-config builds
- **Python 3.x** (optional): For helper scripts in `scripts/`

---

## Building Viper

The platform build scripts configure, compile, test, and install Viper in one step:

```sh
# macOS
./scripts/build_viper_mac.sh

# Linux
./scripts/build_viper_linux.sh

# Windows
scripts\build_viper.cmd
```
The compatibility wrapper `./scripts/build_viper.sh` still dispatches to the correct Unix script on macOS and Linux.

---

## Verify the Installation

After building, confirm the primary tool works correctly:

```sh
viper --version
```

This should display the Viper version and IL version without errors.

---

## Create a New Project

Use `viper init` to scaffold a new project:

```sh
viper init my-app
```

This creates a project directory with a manifest and entry-point source file:

```text
my-app/
  viper.project    # Project manifest (name, version, language, entry point)
  main.zia         # Entry-point source file
```

Run the new project:

```sh
viper run my-app
```

### Options

| Option          | Description                        | Default |
|-----------------|------------------------------------|---------|
| `--lang zia`    | Create a Zia project               | `zia`   |
| `--lang basic`  | Create a BASIC project             | —       |

### Examples

```sh
# Create a Zia project (default)
viper init my-app

# Create a BASIC project
viper init calculator --lang basic
```

---

## Quick Start: Run Your First Program

### BASIC

```sh
viper run examples/basic/ex1_hello_cond.bas
```

**Expected output:**

```text
Hello, World!
Condition is true
```

### Zia

Create a file `hello.zia`:

```rust
module Hello;

func start() {
    Viper.Terminal.Say("Hello, World!");
}
```

Run it:

```sh
viper run hello.zia
```

**Expected output:**

```text
Hello, World!
```

---

## Try the Interactive REPL

For quick experimentation, launch the interactive REPL:

```sh
viper repl
```

The REPL lets you type Zia code and see results immediately:

```text
zia> "Hello from the REPL"
Hello from the REPL
zia> 2 + 3 * 4
14
zia> var x = 42
zia> x
42
zia> func square(n: Integer) -> Integer { return n * n; }
zia> square(7)
49
```

Type `.help` for available commands and `.quit` to exit. See the [REPL Guide](repl.md) for full documentation.

---

## Working with IL Programs

You can inspect the generated IL or run IL programs directly:

```sh
# Emit IL from BASIC
viper build examples/basic/ex1_hello_cond.bas

# Emit IL from Zia
viper build hello.zia

# Save IL to a file
viper build examples/basic/ex1_hello_cond.bas -o hello.il

# Run the IL program directly
ilrun hello.il
```

For more examples, see the **[BASIC Tutorial](basic-language.md)**,
**[Zia Tutorial](zia-getting-started.md)**, and the `examples/` directory.

---

## Command Reference

### Primary Tools

| Tool        | Purpose                                   | Example                     |
|-------------|-------------------------------------------|-----------------------------|
| `viper`     | Unified compiler driver — run and build   | `viper run program.zia`     |
| `viper init`| Scaffold a new project                    | `viper init my-app`         |
| `vbasic`    | Run/compile BASIC programs                | `vbasic script.bas`         |
| `zia`       | Run/compile Zia programs                  | `zia program.zia`           |
| `ilrun`     | Execute IL programs                       | `ilrun program.il`          |
| `il-verify` | Verify IL correctness                     | `il-verify program.il`      |
| `il-dis`    | Disassemble IL                            | `il-dis program.il`         |

> **Note:** `viper run` is the recommended way to run programs. It auto-detects the language
and can run entire project directories.

---

## Key Concepts

### Unified Error Model

All frontends, IL, and the VM share a consistent error and trap model. Diagnostics remain uniform regardless of entry
point.

> **Learn more:** See [specs/errors.md](specs/errors.md) for trap kinds, handler semantics, and BASIC `ON ERROR` lowering rules.

### Deterministic Numerics

Viper guarantees consistent numeric behavior across all platforms and execution modes:

- **Overflow checking**: Zia defaults to checked arithmetic (traps on overflow); BASIC uses wrapping
- **Division**: Zia `/` on integers is integer division; BASIC has separate `/` (float) and `\` (integer)
- **Modulo**: Remainder preserves the dividend's sign (C99 semantics)
- **Conversions**: Casts use checked variants that trap when values are out of range
- **Rounding**: All rounding uses round-to-nearest-even (IEEE 754 default)

> **Learn more:** See [specs/numerics.md](specs/numerics.md) for complete numeric semantics.

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

**Developer Documentation:**

- [architecture.md](architecture.md) — System architecture overview
- [vm.md](vm.md) — VM and runtime internals
- [contributor-guide.md](contributor-guide.md) — Contribution guidelines
