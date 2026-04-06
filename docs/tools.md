---
status: active
audience: public
last-verified: 2026-04-05
---

# CLI Tools Reference

Reference documentation for the Viper command-line tools.

## User-Facing Tools

### zia

Run or compile Zia programs.

```bash
# Run a Zia program
zia program.zia

# Emit IL
zia program.zia --emit-il

# Save IL to file
zia program.zia -o program.il
```

### vbasic

Run or compile BASIC programs.

```bash
# Run a BASIC program
vbasic program.bas

# Emit IL
vbasic program.bas --emit-il

# Save IL to file
vbasic program.bas -o program.il
```

### zia-server

Language server for Zia — serves MCP (for AI assistants) and LSP (for editors).

```bash
# Start in MCP mode (for Claude Code, Copilot, etc.)
zia-server --mcp

# Start in LSP mode (for VS Code)
zia-server --lsp

# Auto-detect protocol from first message
zia-server
```

See [Language Server Reference](zia-server.md) for configuration and tool documentation.

### ilrun

Execute IL programs.

```bash
# Run an IL program
ilrun program.il

# With tracing
ilrun program.il --trace

# With breakpoints
ilrun program.il --break main:10
```

### il-verify

Verify IL correctness.

```bash
il-verify program.il
```

### il-dis

Disassemble IL modules.

```bash
il-dis program.il
```

---

## Advanced Tool: viper

The unified compiler driver provides advanced functionality.

### Overview

The CLI is organized around primary entry points:

- `viper init <name> [--lang zia|basic]` — Scaffold a new project
- `viper run <file|dir>` — Build and run a source file or project
- `viper build <file|dir> [-o out]` — Emit IL or build a native binary
- `viper -run <file.il>` — Execute an IL module
- `viper front zia -emit-il <file.zia>` — Legacy low-level Zia frontend entry point
- `viper front zia -run <file.zia>` — Legacy low-level Zia frontend execution path
- `viper front basic -emit-il <file.bas>` — Legacy low-level BASIC frontend entry point
- `viper front basic -run <file.bas>` — Legacy low-level BASIC frontend execution path
- `viper il-opt <in.il> -o <out.il>` — Run optimization passes
- `viper codegen x64 <in.il> -o <out>` — Compile to x86-64 native code
- `viper codegen arm64 <in.il> -S <out.s>` — Generate ARM64 assembly
- `viper package <dir>` — Package a project for distribution (.app, .deb, .exe, .tar.gz)
- `viper repl` — Launch the interactive REPL

### viper init

Scaffold a new Viper project.

```bash
viper init <project-name> [--lang zia|basic]
```

| Option          | Description                        | Default |
|-----------------|------------------------------------|---------|
| `--lang zia`    | Create a Zia project               | `zia`   |
| `--lang basic`  | Create a BASIC project             | —       |

Creates a project directory containing:
- `viper.project` — Project manifest (name, version, language, entry point)
- `main.zia` or `main.bas` — Entry-point source file with a hello-world template

```bash
viper init my-app
viper run my-app
```

### viper -run

Execute IL modules with debugging controls.

```bash
viper -run <file.il> [flags]
```

| Flag                         | Description                                  |
|------------------------------|----------------------------------------------|
| `--trace=il`                 | Emit line-per-instruction trace              |
| `--trace=src`                | Show source file, line, column for each step |
| `--stdin-from <file>`        | Feed program stdin from file                 |
| `--max-steps <N>`            | Limit execution to N VM steps                |
| `--bounds-checks`            | Enable runtime bounds checking               |
| `--break <Label\|file:line>` | Halt before executing instruction            |
| `--break-src <file:line>`    | Explicit source-line breakpoint              |
| `--debug-cmds <file>`        | Read debugger actions from file              |
| `--step`                     | Enter debug mode, break at entry             |
| `--continue`                 | Ignore breakpoints and run to completion     |
| `--watch <name>`             | Print when scalar changes                    |
| `--count`                    | Print executed instruction count at exit     |
| `--time`                     | Print wall-clock execution time              |

### viper front

Low-level frontend entry points retained for direct compiler testing and
compatibility. Prefer `zia`, `vbasic`, or `viper run` / `viper build` for normal
workflows.

```bash
# Zia
viper front zia -emit-il <file.zia> [--bounds-checks] [--no-runtime-namespaces]
viper front zia -run <file.zia> [--trace=il|src] [--stdin-from <file>] [--max-steps N]

# BASIC
viper front basic -emit-il <file.bas> [--bounds-checks] [--no-runtime-namespaces]
viper front basic -run <file.bas> [--trace=il|src] [--stdin-from <file>] [--max-steps N]
```

### viper il-opt

Run optimization passes on IL modules.

```bash
viper il-opt <in.il> -o <out.il> [flags]
```

| Flag              | Description                        |
|-------------------|------------------------------------|
| `--passes a,b,c`  | Override the pass list             |
| `--no-mem2reg`    | Drop mem2reg from default pipeline |
| `--mem2reg-stats` | Print counts of promoted variables |

Default pipeline: O1 (`simplify-cfg, sccp, constfold, dce, simplify-cfg, sccp, inline, dce, simplify-cfg`)

### viper codegen

Compile IL to native code.

```bash
# x86-64
viper codegen x64 <in.il> -o <executable>
viper codegen x64 <in.il> -S <out.s>  # Assembly only

# ARM64 (Apple Silicon validated)
viper codegen arm64 <in.il> -S <out.s>
```

---

## Exit Codes

| Code | Meaning                                   |
|------|-------------------------------------------|
| `0`  | Program completed successfully            |
| `10` | Halted at breakpoint with no debug script |
| `>0` | Trap or error                             |

---

## CMake Integration

Projects embedding Viper tooling can consume the exported CMake package:

```cmake
find_package(Viper CONFIG REQUIRED)
target_link_libraries(mytool PRIVATE viper::il_core viper::il_io viper::il_vm)
```
