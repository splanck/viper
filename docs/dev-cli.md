# CLI Architecture

`ilc` dispatches to focused handlers based on the initial tokens:

- `-run <file.il> [--trace] [--stdin-from <file>] [--max-steps N] [--bounds-checks]`
- `front basic -emit-il <file.bas> [--bounds-checks]`
- `front basic -run <file.bas> [--trace] [--stdin-from <file>] [--max-steps N] [--bounds-checks]`
- `il-opt <in.il> -o <out.il> --passes p1,p2`

Handlers live in `src/tools/ilc/cmd_run_il.cpp`, `cmd_front_basic.cpp`, and `cmd_il_opt.cpp`.
`src/tools/ilc/main.cpp` merely dispatches to these subcommands.

