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

## Canonical IL

Tools that produce IL for golden tests emit a canonical form:

- one instruction per line with `op arg1, arg2` spacing;
- extern declarations sorted lexicographically;
- globals and functions printed in the order they appear in the module;
- constants rendered deterministically with minimal formatting.

This mode ensures test diffs are stable and comparable across builds.

## Tools

### il-opt

`ilc il-opt` applies optimizer passes to IL modules.

- `peephole`: folds `cbr %cond, label L, label L` into `br label L`.

### VM flags

`ilc -run <file.il>` accepts additional debugging flags:

- `--trace` — print executed instructions.
- `--stdin-from <file>` — read program input from a file.
- `--max-steps <N>` — abort after executing `N` instructions with the message
  `VM: step limit exceeded (N); aborting.`
- `--bounds-checks` — enable debug bounds checks for `DIM` arrays.

## Quick Links

- [Getting Started](/docs/getting-started.md)
