# ilc

## Overview
ilc is an experimental compiler stack centered on a small, well-specified intermediate language (IL). The IL serves as a "thin waist" between source language front ends and eventual native code generation, keeping the core reusable across many languages and targets.

The system is organized as front ends that lower programs into IL, the IL libraries and tools that parse and verify it, and a stack-based virtual machine (VM) that executes IL. Future work will add code generators that translate IL to native code.

Currently the VM and a prototype BASIC front end are functional and can run small examples. Native code generation backends are planned but not yet implemented.

## Quickstart
```sh
cmake -S . -B build && cmake --build build -j
./build/src/tools/il-verify/il-verify docs/examples/il/ex1_hello_cond.il
./build/src/tools/ilc/ilc -run docs/examples/il/ex2_sum_1_to_10.il
```

## Documentation checks

To verify file headers and public API comments locally, run:

```sh
cmake -S . -B build
ctest --test-dir build -L Docs --output-on-failure
```

## Directory layout
```
.
├── src/il              # IL core libraries, parser, and verifier
├── src/vm              # stack-based virtual machine for IL
├── src/frontends/basic # tiny BASIC front end lowering to IL
├── runtime             # C runtime support for the VM
├── docs                # specifications, design notes, and examples
└── tests               # unit, golden, and end-to-end tests
```

## Documentation
- [Docs overview](docs/README.md)
- [IL specification](docs/il-spec.md)
- [Class catalog](docs/class-catalog.md)
- [Project roadmap](docs/roadmap.md)
- [Examples](docs/examples/)

## Contributing
See [AGENTS.md](AGENTS.md) for contribution guidelines and workflow. Architecture changes should start with an ADR as described there.

## License
Licensed under the [MIT License](LICENSE).
