# Codemap

Source code organization for the Viper compiler toolchain.

## IL

| Component                                     | Description                                   |
|-----------------------------------------------|-----------------------------------------------|
| [IL Analysis](codemap/il-analysis.md)         | BasicAA, CallGraph, CFG, dominators           |
| [IL API](codemap/il-api.md)                   | Public API facades                            |
| [IL Build](codemap/il-build.md)               | IR builder for programmatic construction      |
| [IL Core](codemap/il-core.md)                 | IR types, opcodes, modules, functions, values |
| [IL I/O](codemap/il-i-o.md)                   | Text parser/serializer for `.il` files        |
| [IL Link](codemap/il-link.md)                 | Cross-language module linker, boolean thunks  |
| [IL Runtime](codemap/il-runtime.md)           | Runtime signatures, helper effects            |
| [IL Transform](codemap/il-transform.md)       | Optimization passes and pass infra            |
| [IL Utilities](codemap/il-utilities.md)       | Shared IL utilities                           |
| [IL Verification](codemap/il-verification.md) | Verifier (types, control flow, EH)            |

## Frontends

| Component                                      | Description                                |
|------------------------------------------------|--------------------------------------------|
| [BASIC Frontend](codemap/front-end-basic.md)   | Lexer, parser, semantic analysis, lowering |
| [Frontend Common](codemap/front-end-common.md) | Shared utilities across frontends          |
| [Zia Frontend](codemap/front-end-zia.md)       | Zia compiler with entities, generics       |

## Execution & Codegen

| Component                                | Description                               |
|------------------------------------------|-------------------------------------------|
| [Codegen](codemap/codegen.md)            | x86_64 and AArch64 native code generation |
| [Virtual Machine](codemap/vm-runtime.md) | IL interpreter, handlers, debug, bridge   |

## Runtime & Support

| Component                                           | Description                                                  |
|-----------------------------------------------------|--------------------------------------------------------------|
| [Runtime Library (C)](codemap/runtime-library-c.md) | C runtime: strings, arrays, I/O, OOP                         |
| [Support](codemap/support.md)                       | Diagnostics, arena, source manager, symbols, parsing, passes |

## Tools & Libraries

| Component                       | Description                                              |
|---------------------------------|----------------------------------------------------------|
| [Graphics](codemap/graphics.md) | ViperGFX 2D graphics library                             |
| [Tools](codemap/tools.md)       | CLI tools (viper, vbasic, zia, ilrun, il-verify, il-dis) |
| [TUI](codemap/tui.md)           | Terminal UI library + tests                              |

## Additional

- Build metadata: `src/buildmeta/{IL_VERSION,VERSION}`
- Public headers: `include/viper/{diag,il,parse,pass,runtime,vm}`
- Tests: `src/tests/{analysis,e2e,golden,integration,perf,smoke,unit,...}`
