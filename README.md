# ilc

ilc is an experimental compiler stack built around a small intermediate language (IL).

## Goals
- Define a simple, well-specified IL.
- Provide tools and a virtual machine for executing IL.
- Enable future front ends and native code generation.

## Layout
- `src/` – libraries, VM, code generator, and tools.
- `runtime/` – runtime libraries.
- [docs/](docs/README.md) – specifications and planning documents.
- `tests/` – unit, golden, and end-to-end tests.

## Building
Requires a C++20 compiler and CMake.

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Executables will be in `build/src/tools`.
