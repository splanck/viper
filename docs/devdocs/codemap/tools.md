# CODEMAP: Tools

Command-line tools (`src/tools/`) for the Viper toolchain.

Last updated: 2026-01-15

## Overview

- **Total source files**: 39 (.hpp/.cpp)

## User-Facing Tools

### vbasic (`vbasic/`)

| File             | Purpose                                |
|------------------|----------------------------------------|
| `main.cpp`       | BASIC interpreter/compiler entry point |
| `usage.cpp`      | Help text implementation               |
| `usage.hpp`      | Help text and usage information        |
| `ilc_compat.cpp` | Compatibility shim for ilc integration |

### vpascal (`vpascal/`)

| File             | Purpose                                 |
|------------------|-----------------------------------------|
| `main.cpp`       | Pascal interpreter/compiler entry point |
| `usage.cpp`      | Help text implementation                |
| `usage.hpp`      | Help text and usage information         |
| `ilc_compat.cpp` | Compatibility shim for ilc integration  |

### zia (`zia/`)

| File             | Purpose                                |
|------------------|----------------------------------------|
| `main.cpp`       | Zia interpreter/compiler entry point   |
| `usage.cpp`      | Help text implementation               |
| `usage.hpp`      | Help text and usage information        |
| `ilc_compat.cpp` | Compatibility shim for ilc integration |

### ilrun (`ilrun/`)

| File             | Purpose                                |
|------------------|----------------------------------------|
| `main.cpp`       | IL program runner entry point          |
| `ilc_compat.cpp` | Compatibility shim for ilc integration |

### il-verify (`il-verify/`)

| File            | Purpose                     |
|-----------------|-----------------------------|
| `il-verify.cpp` | Standalone IL verifier tool |

### il-dis (`il-dis/`)

| File       | Purpose              |
|------------|----------------------|
| `main.cpp` | IL disassembler demo |

## Advanced Tools

### ilc (`ilc/`)

| File                     | Purpose                             |
|--------------------------|-------------------------------------|
| `main.cpp`               | Unified compiler driver entry point |
| `cli.cpp`                | Shared CLI option parsing impl      |
| `cli.hpp`                | Shared CLI option parsing           |
| `break_spec.cpp`         | Breakpoint specification impl       |
| `break_spec.hpp`         | Breakpoint specification parsing    |
| `cmd_front_basic.cpp`    | BASIC frontend subcommand           |
| `cmd_front_pascal.cpp`   | Pascal frontend subcommand          |
| `cmd_front_zia.cpp`      | Zia frontend subcommand             |
| `cmd_run_il.cpp`         | IL execution subcommand             |
| `cmd_il_opt.cpp`         | IL optimization subcommand          |
| `cmd_bench.cpp`          | Benchmarking subcommand             |
| `cmd_codegen_x64.cpp`    | x86-64 codegen implementation       |
| `cmd_codegen_x64.hpp`    | x86-64 codegen subcommand           |
| `cmd_codegen_arm64.cpp`  | ARM64 codegen implementation        |
| `cmd_codegen_arm64.hpp`  | ARM64 codegen subcommand            |

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

| File                 | Purpose                                   |
|----------------------|-------------------------------------------|
| `frontend_tool.hpp`  | Shared frontend tool utilities            |
| `module_loader.cpp`  | Shared IL module loading implementation   |
| `module_loader.hpp`  | Shared IL module loading with diagnostics |

### basic (`basic/`)

| File         | Purpose                          |
|--------------|----------------------------------|
| `common.cpp` | Shared BASIC tool implementation |
| `common.hpp` | Shared BASIC tool utilities      |
