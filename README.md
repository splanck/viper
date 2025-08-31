# ilc — IL Compiler & BASIC Frontend (VM-first)

ilc is a small compiler stack built around a well-specified intermediate language (IL).
Front ends lower source programs to IL, IL libraries parse and verify it, and a
stack-based virtual machine executes it. Native code generation backends are
planned for a later phase.

## Quickstart

```sh
# Configure and build with Clang
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j

# Verify an IL example
./build/src/tools/il-verify/il-verify docs/examples/il/ex1_hello_cond.il

# Run an IL program via the BASIC frontend
./build/src/tools/ilc/ilc -run docs/examples/il/ex2_sum_1_to_10.il
```

## Directory Layout

```text
.
├── src/il              # IL core libraries, parser, verifier
├── src/vm              # Stack-based virtual machine for IL
├── src/frontends/basic # Experimental BASIC frontend
├── runtime             # C runtime support for the VM
├── docs                # Specifications, design notes, and examples
└── tests               # Unit, golden, and end-to-end tests
```

## Documentation

- [Docs index](docs/README.md)
- [BASIC Language Reference](docs/basic-language-reference.md)
- [IL Specification](docs/il-spec.md)

## Contributing

See [AGENTS.md](AGENTS.md) for contribution workflow and
[Style Guide](docs/style-guide.md) for code and documentation conventions.

## License

Licensed under the [MIT License](LICENSE).

