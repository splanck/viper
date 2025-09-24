---
status: active
audience: public
last-verified: 2025-09-23
---

<a id="ilc"></a>
# ilc — CLI Reference

`ilc` compiles and runs IL modules and front-end sources. It dispatches to
focused handlers based on the first tokens of the command line and routes to the
implementations in `src/tools/ilc/` (for example, `cmd_run_il.cpp`,
`cmd_front_basic.cpp`, and `cmd_il_opt.cpp`).

## Overview

The CLI is organized around three primary entry points:

- `ilc -run <file.il>` — execute an IL module with optional debugging controls.
- `ilc front basic -emit-il <file.bas>` — lower BASIC source code to IL.
- `ilc front basic -run <file.bas>` — compile and execute BASIC programs in one
  step.
- `ilc il-opt <in.il> -o <out.il>` — run optimization passes on IL modules.

Each subcommand shares common runtime options where applicable, such as tracing,
stdin redirection, maximum step counts, and enabling bounds checks.

The archived CLI architecture note spelled out the dispatcher forms explicitly:

- `-run <file.il> [--trace] [--stdin-from <file>] [--max-steps N] [--bounds-checks]`
- `front basic -emit-il <file.bas> [--bounds-checks]`
- `front basic -run <file.bas> [--trace] [--stdin-from <file>] [--max-steps N] [--bounds-checks]`
- `il-opt <in.il> -o <out.il> --passes p1,p2`

## run

### Usage

```
ilc -run <file.il> [flags]
```

### Flags

| Flag | Description |
| ---- | ----------- |
| `--trace=il` | Emit a line-per-instruction trace. |
| `--trace=src` | Show source file, line, and column for each step; falls back to `<unknown>` when locations are missing. See [debug-recursion guide](../dev/debug-recursion.md). |
| `--stdin-from <file>` | Feed program stdin from `file` instead of the terminal. |
| `--max-steps <N>` | Limit execution to `<N>` interpreter steps before aborting. |
| `--bounds-checks` | Enable runtime bounds checking when executing IL. |
| `--break <Label\|file:line>` | Halt before executing the first instruction of block `Label` or the instruction at `file:line`; may be repeated. |
| `--break-src <file>:<line>` | Explicit form of the source-line breakpoint; paths are normalized (platform separators and `.`/`..` segments). If the normalized path does not match, `ilc` falls back to a basename match; may be repeated. |
| `--debug-cmds <file>` | Read debugger actions from `file` when a breakpoint is hit. |
| `--step` | Enter debug mode, break at entry, and step one instruction. |
| `--continue` | Ignore breakpoints and run to completion. |
| `--watch <name>` | Print when scalar `name` changes; may be repeated. |
| `--count` | Print executed instruction count at exit. |
| `--time` | Print wall-clock execution time in milliseconds. |

`--break` inspects its argument. Tokens of the form `<file>:<line>` whose left
side contains a path separator (`/` or `\`) or a dot are interpreted as source
breakpoints. All other tokens are treated as block labels. The `--break-src` flag
remains as an explicit alternative for source breakpoints.

`--time` measures wall-clock time and may vary between runs and systems.

### Examples

Instruction trace:

```
$ ilc -run examples/il/trace_min.il --trace=il
  [IL] fn=@main blk=entry ip=#0 op=add 1, 2 -> %t0
  [IL] fn=@main blk=entry ip=#1 op=mul %t0, 3 -> %t1
  [IL] fn=@main blk=entry ip=#2 op=ret 0
```

Source-line breakpoint:

```
$ ilc -run foo.il --break foo.il:3
  [BREAK] src=foo.il:3 fn=@main blk=entry ip=#0
```

Paths are normalized before comparison. If the normalized path still does not
match the path recorded in the IL, `ilc` compares only the basename and triggers
the breakpoint on a match.

When multiple IL instructions map to the same source line, `ilc` reports a
breakpoint only once per line until control transfers to a different basic
block.

### Non-interactive debugging with --debug-cmds

`ilc` can resume from breakpoints using a scripted command file. Each line
contains a debugger action:

```
step 2
continue
```

`step` executes one instruction; `step N` runs `N` instructions; `continue`
resumes normal execution. Unknown lines are ignored with a `[DEBUG]` message.
Invoke with `--break` to set a breakpoint and `--debug-cmds` to supply the
script:

```
ilc -run examples/il/debug_script.il --break L3 --trace=il --debug-cmds examples/il/debug_script.txt
```

### Watching scalars

Use `--watch` to monitor scalar IL variables by name:

```
ilc -run examples/il/watch_scalars.il --watch x
  [WATCH] x=i64:1  (fn=@main blk=entry ip=#1)
  [WATCH] x=i64:2  (fn=@main blk=entry ip=#3)
```

## front basic

The BASIC front end can either emit IL or execute BASIC programs directly.
Shared runtime flags such as `--trace`, `--stdin-from`, `--max-steps`, and
`--bounds-checks` behave the same as in `ilc -run`.

### Emit IL

```
ilc front basic -emit-il <file.bas> [--bounds-checks]
```

### Run BASIC programs

```
ilc front basic -run <file.bas> [--trace=il|src] [--stdin-from <file>] [--max-steps <N>] [--bounds-checks]
```

Example tracing source locations:

```
$ ilc front basic -run examples/basic/trace_src.bas --trace=src
  [SRC] trace_src.bas:2:4  (fn=@main blk=L20 ip=#1)  PRINT A
```

## il-opt

### Usage

```
ilc il-opt <in.il> -o <out.il> [flags]
```

### Flags

- `--passes a,b,c` — override the pass list.
- `--no-mem2reg` — drop `mem2reg` from the default pipeline.
- `--mem2reg-stats` — print counts of promoted variables and removed loads/stores.

Without `--passes`, the default pipeline is `mem2reg,constfold,peephole,dce`.

### Example

```
ilc il-opt foo.il -o foo.opt.il --mem2reg-stats
```

## CMake integration

Projects embedding Viper tooling can consume the exported CMake package:

```cmake
find_package(Viper CONFIG REQUIRED)
target_link_libraries(mytool PRIVATE viper::il_core viper::il_io viper::il_vm)
```

## Exit codes

| Code | Meaning |
| ---- | ------- |
| `0` | Program completed successfully. |
| `10` | Halted at a breakpoint with no debug script. |
| `>0` | Trap or error. |

Sources:
- docs/tools.md#ilc
- archive/docs/references/ilc.md
- archive/docs/dev-cli.md
