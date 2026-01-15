# Code Map

Source layout for the Viper compiler toolchain (current tree, kept in sync).

> For subsystem deep-dives, see [devdocs/codemap.md](devdocs/codemap.md).
> For auto-generated files, see [generated-files.md](generated-files.md).

## Quick Reference

| Directory              | Purpose                                                                  |
|------------------------|--------------------------------------------------------------------------|
| `src/il/`              | IL core types, builder, I/O, verifier, analysis, transforms, API         |
| `src/vm/`              | Virtual Machine interpreter, opcode handlers, debug, runtime bridge      |
| `src/codegen/`         | Native code generation backends (`x86_64/`, `aarch64/`, `common/`)       |
| `src/frontends/`       | Language frontends: `basic/`, `pascal/`, `zia/`, `common/`         |
| `src/runtime/`         | C runtime library (strings, arrays, I/O, numeric, OOP)                   |
| `src/support/`         | Shared support: diagnostics, arena, source manager, symbols, result      |
| `src/common/`          | Cross-cutting utils (mangling, integer helpers, process runner)          |
| `src/parse/`           | Cursor utilities used by frontends (`include/viper/parse/Cursor.h`)      |
| `src/pass/`            | Generic pass manager façade (`include/viper/pass/PassManager.hpp`)       |
| `src/tools/`           | CLI tools (`ilc`, `vbasic`, `zia`, `vpascal`, `ilrun`, `il-verify`, `il-dis`, etc.) |
| `src/lib/graphics/`    | ViperGFX 2D graphics library (C API, examples, tests)                    |
| `src/tui/`             | Minimal TUI utility + tests (`apps/tui_demo.cpp`)                        |
| `src/tests/`           | Unit, golden, e2e, and perf tests by area                                |
| `include/`             | Public headers (`viper/il`, `viper/vm`, `viper/runtime`, `viper/...`)    |
| `src/buildmeta/`       | Version files (`IL_VERSION`, `VERSION`) used at build time               |

## Subsystems

- IL: `src/il/{core,build,io,verify,analysis,transform,api,utils,runtime,internal}`
- VM: `src/vm` (+ `ops/{generated,common}` and `debug/`)
- Codegen: `src/codegen/{x86_64,aarch64,common}`
- Frontends: `src/frontends/{basic,pascal,zia,common}`
- Runtime: `src/runtime` (C sources and headers)
- Tools: `src/tools/{ilc,vbasic,zia,vpascal,ilrun,il-verify,il-dis,rtgen,basic-ast-dump,basic-lex-dump}`
- Support & Infra: `src/{support,common,parse,pass}`
- Libraries: `src/lib/graphics`, `src/tui`
- Tests: `src/tests/{unit,golden,e2e,smoke,perf,...}`

For architecture and layering, see [architecture.md](architecture.md).
