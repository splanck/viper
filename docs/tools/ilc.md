# ilc

`ilc` compiles and runs IL modules and front-end sources.

## run

`ilc -run <file.il>` executes an IL module.

Flags:

| Flag | Description |
| ---- | ----------- |
| `--trace=il` | emit a line-per-instruction trace. |
| `--trace=src` | show source file, line, and column for each step; falls back to `<unknown>` when locations are missing. |
| `--break <Label>` | halt before executing the first instruction of block `Label`; may be repeated. |
| `--break <file:line>` | halt before executing the first instruction at the given source line. |
| `--debug-cmds <file>` | read debugger actions from `file` when a breakpoint is hit. |
| `--step` | enter debug mode, break at entry, and step one instruction. |
| `--continue` | ignore breakpoints and run to completion. |
| `--watch <name>` | print when scalar `name` changes; may be repeated. |
| `--count` | print executed instruction count at exit. |
| `--time` | print wall-clock execution time in milliseconds. |

`--time` measures wall-clock time and may vary between runs and systems.

An argument to `--break` is interpreted as a source line breakpoint when it
contains a colon following a path with an extension (e.g., `foo.bas:2`).
Label breakpoints halt before entering the block, while source line breakpoints
halt immediately before executing the matching instruction. When both apply to
the same location, the label breakpoint takes precedence and fires first.

Example:

```
$ ilc -run examples/il/trace_min.il --trace=il
  [IL] fn=@main blk=entry ip=#0 op=add 1, 2 -> %t0
  [IL] fn=@main blk=entry ip=#1 op=mul %t0, 3 -> %t1
  [IL] fn=@main blk=entry ip=#2 op=ret 0
```

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

### Exit codes

| Code | Meaning |
| ---- | ------- |
| `0` | program completed successfully |
| `10` | halted at a breakpoint with no debug script |
| `>0` | trap or error |

```
$ ilc front basic -run examples/basic/trace_src.bas --trace=src
  [SRC] trace_src.bas:2:4  (fn=@main blk=L20 ip=#1)  PRINT A
```

## il-opt

`ilc il-opt <in.il> -o <out.il>` runs optimization passes. Without
`--passes`, the default pipeline is `mem2reg,constfold,peephole,dce`.

Flags:

- `--passes a,b,c` — override the pass list.
- `--no-mem2reg` — drop `mem2reg` from the default pipeline.
- `--mem2reg-stats` — print counts of promoted variables and removed
  loads/stores.

Example:

```
ilc il-opt foo.il -o foo.opt.il --mem2reg-stats
```

