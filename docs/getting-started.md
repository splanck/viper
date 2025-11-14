---
status: active
audience: public
last-verified: 2025-11-13
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
./build/src/tools/il-verify/il-verify --help
./build/src/tools/il-dis/il-dis --help
./build/src/tools/ilc/ilc --help
```

Each tool should display its help message without errors.

---

## Run a Simple Example

Try running a BASIC program through the complete toolchain:

```sh
# Run a BASIC program directly on the VM
./build/src/tools/ilc/ilc front basic -run examples/basic/ex1_hello_cond.bas

# Or compile BASIC → IL, then run IL
./build/src/tools/ilc/ilc front basic -emit-il examples/basic/ex1_hello_cond.bas -o hello.il
./build/src/tools/ilc/ilc -run hello.il
```

For more examples, see the **[BASIC Tutorial](basic-language.md)** and the `examples/` directory.

---

## Key Concepts

### Unified Error Model

All frontends, IL, and the VM share a consistent error and trap model. Diagnostics remain uniform regardless of entry point.

> **Learn more:** See `/devdocs/specs/errors.md` for trap kinds, handler semantics, and BASIC `ON ERROR` lowering rules.

### Deterministic Numerics

Viper guarantees consistent numeric behavior across all platforms and execution modes:

- **Overflow checking**: Integer arithmetic traps on overflow instead of wrapping
- **Division operators**: `/` performs floating-point division; `\` truncates toward zero
- **Modulo**: `MOD` preserves the dividend's sign
- **Rounding**: All rounding uses banker's rounding (ties-to-even)
- **Conversions**: Casts use checked variants that trap when values are out of range
- **String conversions**: `VAL` and `STR$` guarantee round-trip consistency

> **Learn more:** See `/devdocs/specs/numerics.md` for complete numeric semantics.

---

## What to Read Next

**Language Documentation:**
- **[BASIC Tutorial](basic-language.md)** — Learn Viper BASIC by example
- **[BASIC Reference](basic-reference.md)** — Complete language reference
- **[IL Guide](il-guide.md)** — Comprehensive IL documentation

**Implementation Guides:**
- **[Frontend How-To](frontend-howto.md)** — Build your own language frontend

**Developer Documentation** (in `/devdocs`):
- `architecture.md` — System architecture overview
- `runtime-vm.md` — VM and runtime internals
- `contributor-guide.md` — Contribution guidelines
- `tools.md` — CLI tools reference
