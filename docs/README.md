# Documentation Index

This directory hosts specifications, plans, and examples for the IL-based
compiler stack. Start here for a quick tour of the project and how to
navigate the available materials. For an overview of the project itself, see
the [root README](../README.md). Automation and contribution guidelines live
in [AGENTS.md](../AGENTS.md).

## Quick links

- [IL specification](il-spec.md)
- [Class catalog](class-catalog.md)
- [Project roadmap](roadmap.md)
- [Examples: BASIC](examples/basic)
- [Examples: IL](examples/il)

## Contributing to docs

Name new files in `kebab-case.md` and place them under `docs/` or an
appropriate subdirectory. Use Markdown and wrap lines at roughly 100
characters. To validate changes locally, build and test the project:

```
cmake -S . -B build
cmake --build build
ctest --output-on-failure
```

Changes to the IL specification require an Architecture Decision Record
(ADR) before implementation; use the template in `adr/000-template.md` if
available.
