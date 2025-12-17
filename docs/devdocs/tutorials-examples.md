---
status: active
audience: public
last-verified: 2025-09-23
---

# Tutorials & Examples

This page gathers the newcomer tutorials and the examples index in one place so you can discover runnable programs
quickly.

<a id="basic-tutorial"></a>

## BASIC Tutorial

Start by running an existing BASIC program through the toolchain:

1. Inspect [`examples/basic/ex1_hello_cond.bas`](../../examples/basic/ex1_hello_cond.bas) to see a minimal program.
2. Lower it to IL with `ilc front basic -emit-il examples/basic/ex1_hello_cond.bas -o hello.il`.
3. Execute the BASIC source directly with `ilc front basic -run examples/basic/ex1_hello_cond.bas` or run the emitted IL
   via `ilc -run hello.il`.
4. Explore more scripts such as [`examples/basic/trace_src.bas`](../../examples/basic/trace_src.bas) to observe
   source-level tracing.

These steps mirror the archived BASIC tutorial stub and demonstrate the full BASIC → IL → VM loop.

<a id="il-tutorial"></a>

## IL Tutorial

For a quick IL tour:

1. Open [`examples/il/watch_scalars.il`](../../examples/il/watch_scalars.il) to see SSA values and tracing hooks in
   action.
2. Run it with `ilc -run examples/il/watch_scalars.il --trace=il` to observe instruction-by-instruction execution.
3. Experiment with `--break` and `--watch` flags as described in the CLI reference to practice debugging IL directly.

Pair this with the [IL Quickstart](../il-guide.md#quickstart) for a deeper explanation of syntax and semantics.

<a id="examples"></a>

## Examples Index

All runnable examples live under the repository’s [`examples/`](../../examples/) directory:

- BASIC programs: [`examples/basic/`](../../examples/basic/) — suitable inputs for `ilc front basic`.
- IL modules: [`examples/il/`](../../examples/il/) — ready to run with `ilc -run`.

Use the CLI options in [Tools → ilc](tools.md#ilc) to run, trace, or debug these programs.

Sources:

- docs/tutorials-examples.md#basic-tutorial
- docs/tutorials-examples.md#il-tutorial
- docs/tutorials-examples.md#examples
