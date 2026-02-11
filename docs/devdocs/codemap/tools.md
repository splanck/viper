# CODEMAP: Tools

Command-line tools (`src/tools/`) for the Viper toolchain.

Last updated: 2026-01-15

## Overview

- **Total source files**: 46 (.hpp/.cpp)

## User-Facing Tools

### vbasic (`vbasic/`)

| File             | Purpose                                |
|------------------|----------------------------------------|
| `main.cpp`       | BASIC interpreter/compiler entry point |
| `usage.cpp`      | Help text implementation               |
| `usage.hpp`      | Help text and usage information        |
| `cli_compat.cpp` | Compatibility shim for viper integration |

### zia (`zia/`)

| File             | Purpose                                |
|------------------|----------------------------------------|
| `main.cpp`       | Zia interpreter/compiler entry point   |
| `usage.cpp`      | Help text implementation               |
| `usage.hpp`      | Help text and usage information        |
| `cli_compat.cpp` | Compatibility shim for viper integration |

### ilrun (`ilrun/`)

| File             | Purpose                                |
|------------------|----------------------------------------|
| `main.cpp`       | IL program runner entry point          |
| `cli_compat.cpp` | Compatibility shim for viper integration |

### il-verify (`il-verify/`)

| File            | Purpose                     |
|-----------------|-----------------------------|
| `il-verify.cpp` | Standalone IL verifier tool |

### il-dis (`il-dis/`)

| File       | Purpose              |
|------------|----------------------|
| `main.cpp` | IL disassembler demo |

## Advanced Tools

### viper (`viper/`)

| File                     | Purpose                             |
|--------------------------|-------------------------------------|
| `main.cpp`               | Unified compiler driver entry point |
| `cli.cpp`                | Shared CLI option parsing impl      |
| `cli.hpp`                | Shared CLI option parsing           |
| `break_spec.cpp`         | Breakpoint specification impl       |
| `break_spec.hpp`         | Breakpoint specification parsing    |
| `cmd_front_basic.cpp`    | BASIC frontend subcommand           |
| `cmd_front_zia.cpp`      | Zia frontend subcommand             |
| `cmd_run_il.cpp`         | IL execution subcommand             |
| `cmd_il_opt.cpp`         | IL optimization subcommand          |
| `cmd_bench.cpp`          | Benchmarking subcommand             |
| `cmd_codegen_x64.cpp`    | x86-64 codegen implementation       |
| `cmd_codegen_x64.hpp`    | x86-64 codegen subcommand           |
| `cmd_codegen_arm64.cpp`  | ARM64 codegen implementation        |
| `cmd_codegen_arm64.hpp`  | ARM64 codegen subcommand            |
| `cmd_run.cpp`            | Run subcommand implementation       |
| `cmd_init.cpp`           | Init subcommand implementation      |

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

| File                   | Purpose                                         |
|------------------------|-------------------------------------------------|
| `frontend_tool.hpp`    | Shared frontend tool utilities                  |
| `module_loader.cpp`    | Shared IL module loading implementation         |
| `module_loader.hpp`    | Shared IL module loading with diagnostics       |
| `source_loader.cpp`    | Source file loading implementation              |
| `source_loader.hpp`    | Source file loading utilities                   |
| `native_compiler.cpp`  | Native compilation driver implementation        |
| `native_compiler.hpp`  | Native compilation driver                       |
| `project_loader.cpp`   | Multi-file project loading implementation       |
| `project_loader.hpp`   | Multi-file project loading utilities            |
| `vm_executor.cpp`      | VM execution wrapper implementation             |
| `vm_executor.hpp`      | VM execution wrapper utilities                  |
| `CommonUsage.hpp`      | Shared usage/help text utilities                |
| `ArgvView.hpp`         | Argument vector view utility                    |

### basic (`basic/`)

| File         | Purpose                          |
|--------------|----------------------------------|
| `common.cpp` | Shared BASIC tool implementation |
| `common.hpp` | Shared BASIC tool utilities      |
