---
status: active
audience: contributors
last-verified: 2026-03-04
---

# CODEMAP: Tools

Command-line tools (`src/tools/`) for the Viper toolchain.

## Overview

- **Total source files**: 61 (.hpp/.cpp)

## User-Facing Tools

### vbasic (`vbasic/`)

| File             | Purpose                                  |
|------------------|------------------------------------------|
| `cli_compat.cpp` | Compatibility shim for viper integration |
| `main.cpp`       | BASIC VM/compiler entry point            |
| `usage.cpp`      | Help text implementation                 |
| `usage.hpp`      | Help text and usage information          |

### zia (`zia/`)

| File             | Purpose                                  |
|------------------|------------------------------------------|
| `cli_compat.cpp` | Compatibility shim for viper integration |
| `main.cpp`       | Zia VM/compiler entry point              |
| `usage.cpp`      | Help text implementation                 |
| `usage.hpp`      | Help text and usage information          |

### ilrun (`ilrun/`)

| File             | Purpose                                  |
|------------------|------------------------------------------|
| `cli_compat.cpp` | Compatibility shim for viper integration |
| `main.cpp`       | IL program runner entry point            |

### il-verify (`il-verify/`)

| File            | Purpose                     |
|-----------------|-----------------------------|
| `il-verify.cpp` | Standalone IL verifier tool |

### il-dis (`il-dis/`)

| File       | Purpose              |
|------------|----------------------|
| `main.cpp` | IL disassembler demo |

### zia-server (`zia-server/`)

| File | Purpose |
|------|---------|
| `Json.hpp` / `Json.cpp` | Zero-dependency JSON value type, parser, emitter |
| `Transport.hpp` / `Transport.cpp` | MCP (newline) and LSP (Content-Length) transports |
| `JsonRpc.hpp` / `JsonRpc.cpp` | JSON-RPC 2.0 request/response types and builders |
| `CompilerBridge.hpp` / `CompilerBridge.cpp` | Protocol-agnostic facade wrapping fe_zia APIs |
| `McpHandler.hpp` / `McpHandler.cpp` | MCP lifecycle + 11 tool definitions + dispatch |
| `LspHandler.hpp` / `LspHandler.cpp` | LSP capabilities + request/notification handlers |
| `DocumentStore.hpp` / `DocumentStore.cpp` | In-memory document store for LSP open files |
| `main.cpp` | Entry point with --mcp/--lsp/auto-detect |

For full details, see [codemap/zia-server.md](zia-server.md).

## Advanced Tools

### viper (`viper/`)

| File                    | Purpose                             |
|-------------------------|-------------------------------------|
| `break_spec.cpp`        | Breakpoint specification impl       |
| `break_spec.hpp`        | Breakpoint specification parsing    |
| `cli.cpp`               | Shared CLI option parsing impl      |
| `cli.hpp`               | Shared CLI option parsing           |
| `cmd_bench.cpp`         | Benchmarking subcommand             |
| `cmd_codegen_arm64.cpp` | ARM64 codegen implementation        |
| `cmd_codegen_arm64.hpp` | ARM64 codegen subcommand            |
| `cmd_codegen_x64.cpp`   | x86-64 codegen implementation       |
| `cmd_codegen_x64.hpp`   | x86-64 codegen subcommand           |
| `cmd_front_basic.cpp`   | BASIC frontend subcommand           |
| `cmd_front_zia.cpp`     | Zia frontend subcommand             |
| `cmd_il_opt.cpp`        | IL optimization subcommand          |
| `cmd_init.cpp`          | Init subcommand implementation      |
| `cmd_run.cpp`           | Run subcommand implementation       |
| `cmd_run_il.cpp`        | IL execution subcommand             |
| `main.cpp`              | Unified compiler driver entry point |

### rtgen (`rtgen/`)

| File        | Purpose                                                |
|-------------|--------------------------------------------------------|
| `rtgen.cpp` | Runtime signature generator from runtime.def + headers |

## Debugging Tools

### basic-ast-dump (`basic-ast-dump/`)

| File       | Purpose              |
|------------|----------------------|
| `main.cpp` | BASIC AST visualizer |

### basic-lex-dump (`basic-lex-dump/`)

| File       | Purpose                  |
|------------|--------------------------|
| `main.cpp` | BASIC lexer token dumper |

## Shared Utilities

### common (`common/`)

| File                  | Purpose                                   |
|-----------------------|-------------------------------------------|
| `ArgvView.hpp`        | Argument vector view utility              |
| `CommonUsage.hpp`     | Shared usage/help text utilities          |
| `frontend_tool.hpp`   | Shared frontend tool utilities            |
| `module_loader.cpp`   | Shared IL module loading implementation   |
| `module_loader.hpp`   | Shared IL module loading with diagnostics |
| `native_compiler.cpp` | Native compilation driver implementation  |
| `native_compiler.hpp` | Native compilation driver                 |
| `project_loader.cpp`  | Multi-file project loading implementation |
| `project_loader.hpp`  | Multi-file project loading utilities      |
| `source_loader.cpp`   | Source file loading implementation        |
| `source_loader.hpp`   | Source file loading utilities             |
| `vm_executor.cpp`     | VM execution wrapper implementation       |
| `vm_executor.hpp`     | VM execution wrapper utilities            |

### basic (`basic/`)

| File         | Purpose                          |
|--------------|----------------------------------|
| `common.cpp` | Shared BASIC tool implementation |
| `common.hpp` | Shared BASIC tool utilities      |
