# Developer CLI Structure

The `ilc` tool delegates top-level commands to dedicated handlers:

- `-run <file.il>` — executed by `cmdRunIL`.
- `front basic` — handled by `cmdFrontBasic` for BASIC sources.
- `il-opt` — optimized by `cmdILOpt`.

`main.cpp` parses the initial tokens and forwards arguments to the
appropriate handler, keeping the usage text in one place. BASIC
compilation shares the `compileBasicToIL` helper used by both
`-emit-il` and `-run` modes.

