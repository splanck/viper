# Code Map

Source code organization for the Viper compiler toolchain.

> **Full documentation**: See [/devdocs/codemap.md](/devdocs/codemap.md) for detailed file-by-file breakdowns.

## Quick Reference

| Directory | Purpose |
|-----------|---------|
| `src/il/` | Intermediate Language core, builder, I/O, verifier, transforms |
| `src/vm/` | Virtual machine interpreter |
| `src/codegen/` | Native code generation (x86-64, ARM64) |
| `src/frontends/basic/` | BASIC frontend (lexer, parser, semantic analysis, lowering) |
| `src/runtime/` | C runtime library (strings, arrays, I/O, OOP) |
| `src/support/` | Shared utilities (arena, diagnostics, source manager) |
| `src/tools/` | CLI tools (vbasic, ilrun, ilc, il-verify, il-dis) |
| `src/lib/graphics/` | ViperGFX 2D graphics library |
| `src/tui/` | Terminal UI library |

## Detailed Codemaps

See `/devdocs/codemap/` for detailed breakdowns:

- **IL Layer**: il-core, il-build, il-i-o, il-verification, il-analysis, il-transform
- **Frontends**: front-end-basic
- **Backends**: codegen (x86-64, ARM64), vm-runtime
- **Runtime**: runtime-library-c
- **Tools**: tools, support, graphics, tui

For architecture overview, see [architecture.md](architecture.md).
