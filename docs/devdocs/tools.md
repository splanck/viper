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

### vpascal

Run or compile Pascal programs.

```bash
# Run a Pascal program
vpascal program.pas

# Emit IL
vpascal program.pas --emit-il

# Save IL to file
vpascal program.pas -o program.il
```

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

- `viper -run <file.il>` — Execute an IL module
- `viper front zia -emit-il <file.zia>` — Lower Zia to IL
- `viper front zia -run <file.zia>` — Compile and execute Zia
- `viper front basic -emit-il <file.bas>` — Lower BASIC to IL
- `viper front basic -run <file.bas>` — Compile and execute BASIC
- `viper front pascal -emit-il <file.pas>` — Lower Pascal to IL
- `viper front pascal -run <file.pas>` — Compile and execute Pascal
- `viper il-opt <in.il> -o <out.il>` — Run optimization passes
- `viper codegen x64 <in.il> -o <out>` — Compile to x86-64 native code (experimental; unvalidated on real x86)
- `viper codegen arm64 <in.il> -S <out.s>` — Generate ARM64 assembly

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
| `--max-steps <N>`            | Limit execution to N interpreter steps       |
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

Compile programs from any supported frontend (zia, basic, pascal).

```bash
# Zia
viper front zia -emit-il <file.zia> [--bounds-checks]
viper front zia -run <file.zia> [--trace=il|src] [--stdin-from <file>]

# BASIC
viper front basic -emit-il <file.bas> [--bounds-checks]
viper front basic -run <file.bas> [--trace=il|src] [--stdin-from <file>]

# Pascal
viper front pascal -emit-il <file.pas> [--bounds-checks]
viper front pascal -run <file.pas> [--trace=il|src] [--stdin-from <file>]
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

Default pipeline: `mem2reg,constfold,peephole,dce`

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
