# Code Map

Source layout for the Viper compiler toolchain (current tree, kept in sync).

> For subsystem deep-dives, see [devdocs/codemap.md](devdocs/codemap.md).
> For auto-generated files, see [generated-files.md](generated-files.md).

## Quick Reference

| Directory              | Purpose                                                                  |
|------------------------|--------------------------------------------------------------------------|
| `src/buildmeta/`       | Version files (`IL_VERSION`, `VERSION`) used at build time               |
| `src/bytecode/`        | Bytecode VM: compiler, module format, and high-performance interpreter   |
| `src/codegen/`         | Native code generation backends (`x86_64/`, `aarch64/`, `common/`)       |
| `src/common/`          | Cross-cutting utils (mangling, integer helpers, process runner)          |
| `src/frontends/`       | Language frontends: `basic/`, `zia/`, `common/`                          |
| `src/il/`              | IL core types, builder, I/O, verifier, analysis, transforms, API         |
| `src/lib/graphics/`    | ViperGFX 2D graphics library (C API, examples, tests)                    |
| `src/parse/`           | Cursor utilities used by frontends (`include/viper/parse/Cursor.h`)      |
| `src/pass/`            | Generic pass manager façade (`include/viper/pass/PassManager.hpp`)       |
| `src/runtime/`         | C runtime library (strings, collections, I/O, math, graphics, audio, input, networking, threading, text, time, crypto, GC, serialization, physics, async) |
| `src/support/`         | Shared support: diagnostics, arena, source manager, symbols, result      |
| `src/tests/`           | Unit, golden, e2e, and perf tests by area                                |
| `src/tools/`           | CLI tools (`viper`, `vbasic`, `zia`, `ilrun`, `il-verify`, `il-dis`, etc.) |
| `src/tui/`             | Terminal UI library + demo app (`src/tui/apps/tui_demo.cpp`)             |
| `src/vm/`              | Virtual Machine interpreter, opcode handlers, debug, runtime bridge      |
| `include/`             | Public headers (`viper/il`, `viper/vm`, `viper/runtime`, `viper/...`)    |

## Subsystems

- Bytecode: `src/bytecode` (compiler, module, VM implementation)
- Codegen: `src/codegen/{aarch64,common,x86_64}`
- Frontends: `src/frontends/{basic,common,zia}`
- IL: `src/il/{analysis,api,build,core,internal,io,runtime,transform,utils,verify}`
- Libraries: `src/lib/graphics`, `src/tui`
- Runtime: `src/runtime` (C sources and headers)
- Support & Infra: `src/{common,parse,pass,support}`
- Tests: `src/tests/{e2e,golden,perf,smoke,unit,...}`
- Tools: `src/tools/{basic,basic-ast-dump,basic-lex-dump,common,il-dis,il-verify,ilrun,rtgen,vbasic,viper,zia}`
- VM: `src/vm` (+ `ops/{common,generated}` and `debug/`)

For architecture and layering, see [architecture.md](architecture.md).
