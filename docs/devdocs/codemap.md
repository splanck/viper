# Codemap

Source code organization for the Viper compiler toolchain.

## IL

| Component                                     | Description                                   |
|-----------------------------------------------|-----------------------------------------------|
| [IL Core](codemap/il-core.md)                 | IR types, opcodes, modules, functions, values |
| [IL Build](codemap/il-build.md)               | IR builder for programmatic construction      |
| [IL I/O](codemap/il-i-o.md)                   | Text parser/serializer for `.il` files        |
| [IL Verification](codemap/il-verification.md) | Verifier (types, control flow, EH)            |
| [IL Analysis](codemap/il-analysis.md)         | CFG, dominators, alias analysis               |
| [IL Transform](codemap/il-transform.md)       | Optimization passes and pass infra            |
| [IL Runtime](codemap/il-runtime.md)           | Runtime signatures, helper effects            |
| [IL Utilities](codemap/il-utilities.md)       | Shared IL utilities                           |
| [IL API](codemap/il-api.md)                   | Public API facades                            |

## Frontends

| Component                                    | Description                                |
|----------------------------------------------|--------------------------------------------|
| [BASIC Frontend](codemap/front-end-basic.md) | Lexer, parser, semantic analysis, lowering |

## Execution & Codegen

| Component                                | Description                               |
|------------------------------------------|-------------------------------------------|
| [Virtual Machine](codemap/vm-runtime.md) | IL interpreter, handlers, debug, bridge   |
| [Codegen](codemap/codegen.md)            | x86_64 and AArch64 native code generation |

## Runtime & Support

| Component                                           | Description                                                  |
|-----------------------------------------------------|--------------------------------------------------------------|
| [Runtime Library (C)](codemap/runtime-library-c.md) | C runtime: strings, arrays, I/O, OOP                         |
| [Support](codemap/support.md)                       | Diagnostics, arena, source manager, symbols, parsing, passes |

## Tools & Libraries

| Component                       | Description                                       |
|---------------------------------|---------------------------------------------------|
| [Tools](codemap/tools.md)       | CLI tools (ilc, vbasic, ilrun, il-verify, il-dis) |
| [Graphics](codemap/graphics.md) | ViperGFX 2D graphics library                      |
| [TUI](codemap/tui.md)           | Minimal TUI utility + tests                       |

## Additional

- Public headers: `include/viper/{il,vm,runtime,parse,pass}`
- Tests: `src/tests/{unit,golden,e2e,smoke,perf,...}`
- Build metadata: `src/buildmeta/{IL_VERSION,VERSION}`
