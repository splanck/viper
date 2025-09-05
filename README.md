# Viper

**IL Compiler & BASIC Frontend (VM-first)**

Viper is an experimental compiler stack centered on a small, well-specified Intermediate Language (IL).
Language frontends (currently a tiny BASIC) lower source programs to IL, IL libraries parse and verify modules, and a stack-based virtual machine executes IL today.
The architecture flows from frontends → IL → VM, with native code generators planned for future phases.

## Quickstart

```sh
# Configure and build with Clang
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build

# Run a BASIC example on the VM
./build/src/tools/ilc/ilc front basic -run docs/examples/basic/ex1_hello_cond.bas
```

## Cleaning

Out-of-source builds (for example, using a dedicated `build/` directory) are recommended.

- **Buildsystem clean** (portable):
  ```sh
  cmake --build build --target clean
  ```
  For multi-config generators such as MSVC or Xcode, add `--config Debug` or `--config Release`.

- **Full purge**: remove the entire build directory for a fresh start.
  ```sh
  rm -rf build
  ```
  Convenience scripts will be added in later prompts.

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

## Documentation

- [Docs index](docs/README.md)
- [BASIC language reference](docs/basic-language-reference.md)
- [IL specification](docs/il-spec.md)
- [Getting Started](/docs/getting-started.md)

## Contributing

See [AGENTS.md](AGENTS.md) for contribution workflow and the [style guide](docs/style-guide.md) for formatting rules.
Run `cmake --build build --target format` to apply clang-format to all `.cpp` and `.hpp` files before committing.

## License

Licensed under the [MIT License](LICENSE).
