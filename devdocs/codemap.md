# Codemap

Source code organization for the Viper compiler toolchain.

## Core Components

| Component | Description |
|-----------|-------------|
| [IL Core](codemap/il-core.md) | IR types, opcodes, modules, functions, values |
| [IL Build](codemap/il-build.md) | IR builder for programmatic construction |
| [IL I/O](codemap/il-i-o.md) | Text parser/serializer for `.il` files |
| [IL Verification](codemap/il-verification.md) | Verifier (types, control flow, EH) |
| [IL Analysis](codemap/il-analysis.md) | CFG, dominators, alias analysis |
| [IL Transform](codemap/il-transform.md) | Optimization passes |
| [IL Runtime](codemap/il-runtime.md) | Runtime signature metadata |
| [IL Utilities](codemap/il-utilities.md) | Shared IL utilities |
| [IL API](codemap/il-api.md) | Public API facades |

## Frontends

| Component | Description |
|-----------|-------------|
| [BASIC Frontend](codemap/front-end-basic.md) | Lexer, parser, semantic analysis, lowering |

## Backends

| Component | Description |
|-----------|-------------|
| [Codegen](codemap/codegen.md) | x86-64 and ARM64 native code generation |
| [VM Runtime](codemap/vm-runtime.md) | Virtual machine interpreter |

## Runtime & Support

| Component | Description |
|-----------|-------------|
| [Runtime Library (C)](codemap/runtime-library-c.md) | C runtime: strings, arrays, I/O, OOP |
| [Support](codemap/support.md) | Shared utilities (arena, diagnostics, source manager) |

## Tools & Libraries

| Component | Description |
|-----------|-------------|
| [Tools](codemap/tools.md) | CLI tools (vbasic, ilrun, ilc, il-verify) |
| [Graphics](codemap/graphics.md) | ViperGFX 2D graphics library |
| [TUI](codemap/tui.md) | Terminal UI library |

## Documentation

| Component | Description |
|-----------|-------------|
| [Docs](codemap/docs.md) | User-facing documentation |
