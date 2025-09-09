<!--
File: README.md
Purpose: Project overview, build, testing, and documentation links.
-->

# Viper

**IL Compiler & BASIC Frontend (VM-first)**

Viper is an experimental compiler stack centered on a small, well-specified
Intermediate Language (IL). Language frontends (currently a tiny BASIC) lower
programs to IL. Libraries parse and verify modules, and a stack-based virtual
machine executes IL today. Native code generation is planned for future phases.

## Quick Links

- [Getting Started](docs/getting-started.md)
- [Docs index](docs/index.md)
- [BASIC reference](docs/references/basic.md)
- [IL specification](docs/references/il.md)

## Build

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

## Test

```sh
ctest --test-dir build --output-on-failure
```

## Run an example

```sh
./build/src/tools/ilc/ilc front basic -run docs/examples/basic/ex1_hello_cond.bas
```

## Directory layout

```text
.
├── src/               # C++ sources: IL core, VM, frontends
│   ├── il/            # IL core libraries, parser, verifier
│   ├── vm/            # Stack-based virtual machine
│   └── frontends/     # Language frontends (BASIC implemented)
├── runtime/           # C runtime support for the VM
├── docs/              # Specifications, guides, and examples
├── tests/             # Unit, golden, and end-to-end tests
├── scripts/           # Build and utility helpers
└── CMakeLists.txt     # Top-level build configuration
```

## Contributing

See [AGENTS.md](AGENTS.md) for workflow guidance and the
[style guide](docs/style-guide.md) for formatting rules. Run
`cmake --build build --target format` to apply clang-format before committing.

## License

Licensed under the [MIT License](LICENSE).
