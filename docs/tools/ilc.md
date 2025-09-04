# ilc

`ilc` compiles and runs IL modules and front-end sources.

## run

`ilc -run <file.il>` executes an IL module.

Flags:

- `--trace=il` — emit a line-per-instruction trace.
- `--trace=src` — show source file, line, and column for each step; falls back to
  `<unknown>` when locations are missing.
- `--break <Label>` — break before the block's first instruction; may be repeated.

Example:

```
$ ilc -run examples/il/trace_min.il --trace=il
  [IL] fn=@main blk=entry ip=#0 op=add 1, 2 -> %t0
  [IL] fn=@main blk=entry ip=#1 op=mul %t0, 3 -> %t1
  [IL] fn=@main blk=entry ip=#2 op=ret 0
```

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

