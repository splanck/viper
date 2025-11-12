---
status: active
audience: public
last-verified: 2025-09-24
---

## Prerequisites

Set up the core toolchain before cloning the repository:
- CMake ≥ 3.20.
- A C++20-capable compiler (GCC, Clang, or MSVC) and optionally Ninja for faster multi-config builds.
- Python 3.x if you plan to run helper scripts under `scripts/`.

## Build (Linux/macOS/Windows)

Use an out-of-source build directory so you can cleanly rebuild or switch configurations.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

If you want a relocatable tree for packaging or downstream testing, install into a local prefix:

```sh
cmake --install build --prefix ./_install
```

## Quick sanity check

After the build finishes, confirm the primary tools respond to `--help`:

```sh
./build/src/tools/il-verify/il-verify --help
./build/src/tools/il-dis/il-dis --help
./build/src/tools/ilc/ilc --help
```

Explore the [tutorials-examples.md#examples](tutorials-examples.md#examples) section to run a sample end-to-end once the binaries are in place.

## Unified Errors

All front ends, the IL, and the VM share a single error and trap model so diagnostics stay consistent regardless of the entry point. Review [specs/errors.md](specs/errors.md) for the full set of trap kinds, handler semantics, and BASIC `ON ERROR` lowering rules before wiring new behavior into the stack.

## Precise Numerics

Viper's execution model ships with deterministic numeric semantics ([specs/numerics.md](specs/numerics.md)) so IL and BASIC behave identically across interpreters and builds:

- Integer arithmetic traps on overflow instead of wrapping, keeping logic predictable.
- `/` always performs floating-point division while `\` truncates toward zero; `MOD` keeps the dividend's sign.
- All rounding follows banker's rounding (ties-to-even) and casts use checked variants that raise when a value is out of range.
- `VAL` and `STR$` guarantee consistent parse/print round-trips without locale surprises.

## What to read next

- Architecture → [architecture.md](architecture.md)
- BASIC language → [basic-language.md](basic-language.md)
- IL guide → [il-guide.md](il-guide.md)
- Runtime & VM → [runtime-vm.md](runtime-vm.md)
- Tools (CLI) → [tools.md](tools.md)
- Tutorials & Examples → [tutorials-examples.md](tutorials-examples.md)
- Contributor Guide → [contributor-guide.md](contributor-guide.md)
