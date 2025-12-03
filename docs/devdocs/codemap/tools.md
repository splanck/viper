# CODEMAP: Tools

Command-line tools (`src/tools/`) for the Viper toolchain.

## User-Facing Tools

### vbasic (`vbasic/`)

| File | Purpose |
|------|---------|
| `main.cpp` | BASIC interpreter/compiler entry point |
| `usage.hpp/cpp` | Help text and usage information |
| `ilc_compat.cpp` | Compatibility shim for ilc integration |

### ilrun (`ilrun/`)

| File | Purpose |
|------|---------|
| `main.cpp` | IL program runner entry point |
| `ilc_compat.cpp` | Compatibility shim for ilc integration |

### il-verify (`il-verify/`)

| File | Purpose |
|------|---------|
| `il-verify.cpp` | Standalone IL verifier tool |

### il-dis (`il-dis/`)

| File | Purpose |
|------|---------|
| `main.cpp` | IL disassembler demo |

## Advanced Tools

### ilc (`ilc/`)

| File | Purpose |
|------|---------|
| `main.cpp` | Unified compiler driver entry point |
| `cli.hpp/cpp` | Shared CLI option parsing |
| `break_spec.hpp/cpp` | Breakpoint specification parsing |
| `cmd_front_basic.cpp` | BASIC frontend subcommand |
| `cmd_run_il.cpp` | IL execution subcommand |
| `cmd_il_opt.cpp` | IL optimization subcommand |
| `cmd_codegen_x64.hpp/cpp` | x86-64 codegen subcommand |
| `cmd_codegen_arm64.hpp/cpp` | ARM64 codegen subcommand |

## Debugging Tools

### basic-ast-dump (`basic-ast-dump/`)

| File | Purpose |
|------|---------|
| `main.cpp` | BASIC AST visualizer |

### basic-lex-dump (`basic-lex-dump/`)

| File | Purpose |
|------|---------|
| `main.cpp` | BASIC lexer token dumper |

## Shared Utilities

### common (`common/`)

| File | Purpose |
|------|---------|
| `module_loader.hpp/cpp` | Shared IL module loading with diagnostics |

### basic (`basic/`)

| File | Purpose |
|------|---------|
| `common.hpp/cpp` | Shared BASIC tool utilities |
