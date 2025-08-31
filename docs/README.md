# Documentation index

The `docs/` directory contains language references, design notes, and examples for the
IL-based compiler stack. For an overview of the project, see the [root README](../README.md).

## Table of contents

- [basic-language-reference.md](basic-language-reference.md) — syntax & semantics of BASIC v0.1.
- [il-spec.md](il-spec.md) — IL v0.1.1 normative spec.
- [class-catalog.md](class-catalog.md) — C++ class catalog.
- [roadmap.md](roadmap.md) — milestones and current status.
- [style-guide.md](style-guide.md) — doc & comment conventions.
- [examples/](examples/) — sample BASIC + IL programs.

## Contributing to docs

Follow the [style guide](style-guide.md) when editing documentation.
Run `scripts/check_docs_exist` to verify referenced files exist.
After updating docs, rebuild and test the project:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Return to the [project README](../README.md).
