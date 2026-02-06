---
status: active
audience: public
last-verified: 2026-02-02
---

# Tutorials & Examples

This page gathers the newcomer tutorials and the examples index in one place so you can discover runnable programs
quickly.

<a id="zia-tutorial"></a>

## Zia Tutorial

Zia is Viper's primary language. Start by running an existing Zia program:

1. Inspect [`demos/zia/frogger/main.zia`](../../demos/zia/frogger/main.zia) to see a complete game example.
2. Lower it to IL with `viper front zia -emit-il demos/zia/frogger/main.zia -o frogger.il`.
3. Execute the Zia source directly with `viper front zia -run demos/zia/frogger/main.zia` or run the emitted IL
   via `viper -run frogger.il`.
4. Explore more demos under [`demos/zia/`](../../demos/zia/) to see modules, entities, and graphics in action.

These steps demonstrate the full Zia → IL → VM loop.

<a id="basic-tutorial"></a>

## BASIC Tutorial

Start by running an existing BASIC program through the toolchain:

1. Inspect [`examples/basic/ex1_hello_cond.bas`](../../examples/basic/ex1_hello_cond.bas) to see a minimal program.
2. Lower it to IL with `viper front basic -emit-il examples/basic/ex1_hello_cond.bas -o hello.il`.
3. Execute the BASIC source directly with `viper front basic -run examples/basic/ex1_hello_cond.bas` or run the emitted IL
   via `viper -run hello.il`.
4. Explore more scripts such as [`examples/basic/trace_src.bas`](../../examples/basic/trace_src.bas) to observe
   source-level tracing.

These steps demonstrate the full BASIC → IL → VM loop.

<a id="il-tutorial"></a>

## IL Tutorial

For a quick IL tour:

1. Open [`examples/il/watch_scalars.il`](../../examples/il/watch_scalars.il) to see SSA values and tracing hooks in
   action.
2. Run it with `viper -run examples/il/watch_scalars.il --trace=il` to observe instruction-by-instruction execution.
3. Experiment with `--break` and `--watch` flags as described in the CLI reference to practice debugging IL directly.

Pair this with the [IL Quickstart](../il-guide.md#quickstart) for a deeper explanation of syntax and semantics.

<a id="examples"></a>

## Examples Index

All runnable examples live under the repository's [`examples/`](../../examples/) and [`demos/`](../../demos/) directories:

### Language Examples

- Zia programs: [`demos/zia/`](../../demos/zia/) — substantial demos including games, graphics, and applications.
- BASIC programs: [`examples/basic/`](../../examples/basic/) — simple examples suitable for `viper front basic`.
- IL modules: [`examples/il/`](../../examples/il/) — ready to run with `viper -run`.

### Game Demos

- [`demos/zia/frogger/`](../../demos/zia/frogger/) — Complete Frogger game in Zia demonstrating modules and game architecture.
- [`demos/zia/ladders/`](../../demos/zia/ladders/) — Platform game demonstrating entities and collision detection.
- [`demos/basic/vtris/`](../../demos/basic/vtris/) — Tetris game in BASIC demonstrating OOP and graphics.
- [`demos/basic/frogger/`](../../demos/basic/frogger/) — Frogger game in BASIC.

Use the CLI options in [Tools → viper](tools.md#viper) to run, trace, or debug these programs.

Sources:

- docs/tutorials-examples.md#zia-tutorial
- docs/tutorials-examples.md#basic-tutorial
- docs/tutorials-examples.md#il-tutorial
- docs/tutorials-examples.md#examples
