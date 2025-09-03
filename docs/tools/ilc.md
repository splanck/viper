# ilc

`ilc` compiles and runs IL modules and front-end sources.

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

