<!--
File: README.md
Purpose: High-signal landing page with build and run instructions.
-->

# Viper — IL-first Compiler Stack (BASIC → IL v0.1.2 → VM)

Viper is a small, deterministic compiler toolchain:

- **Front end:** BASIC lowers to a minimal, well-typed [IL v0.1.2](docs/il-guide.md#reference).
- **Runtime/VM:** A portable C runtime and C++ VM execute IL directly.
- **Backends:** Native codegen is deferred until IL + VM + front ends are solid.

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run Your First Programs

```bash
# BASIC factorial
./build/src/tools/ilc/ilc front basic examples/basic/fact.bas -run
# prints 3628800

# IL hello
./build/src/tools/ilc/ilc -run examples/il/ex1_hello_cond.il
```

## Documentation

- [Getting Started](docs/getting-started.md)
- [BASIC Reference](docs/basic-language.md)
- [IL Spec (v0.1.2)](docs/il-guide.md#reference)
- [Lowering (BASIC → IL)](docs/il-guide.md#lowering)
- [Tutorials: BASIC](docs/tutorials-examples.md#basic-tutorial), [IL](docs/tutorials-examples.md#il-tutorial)
- [Architecture (contributors)](docs/architecture.md)
- [CLI (ilc)](docs/tools.md#ilc)
- Examples: [BASIC](examples/basic), [IL](examples/il)

## Layout

```
/examples     sample BASIC & IL programs (canonical)
/docs         references, tutorials, dev notes
/lib          IL core, VM, front ends, passes
/runtime      C runtime used by VM
/tests        unit, golden, and e2e tests
```

## Contributing & License

See [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md). License: see [LICENSE](LICENSE).
