<!--
File: docs/examples.md
Purpose: Overview of example programs and how to run them.
-->

# Examples

A few BASIC samples illustrate the runtime. Top-level BASIC statements are lowered into an IL function named `@main`.

## Monte Carlo π

`examples/basic/monte_carlo_pi.bas` estimates π by sampling points inside the unit square.

Run:

```sh
ilc front basic -run examples/basic/monte_carlo_pi.bas
```

## Random walk

`examples/basic/random_walk.bas` performs a 100 step one-dimensional random walk with a fixed seed.

Run:

```sh
ilc front basic -run examples/basic/random_walk.bas
```

## Factorial

`examples/basic/fact.bas` computes `FACT(10)` recursively and prints `3628800`.

Run:

```sh
ilc front basic -run examples/basic/fact.bas
```

## Fibonacci

`examples/basic/fib.bas` computes `FIB(10)` recursively and prints `55`. It finishes in roughly 180 VM steps, so a guard of 200 suffices if using `--max-steps`.

Run:

```sh
ilc front basic -run examples/basic/fib.bas
```

## String builder

`examples/basic/string_builder.bas` defines `FUNCTION EXCL` that appends `!` to its argument and prints `hi!`.

Run:

```sh
ilc front basic -run examples/basic/string_builder.bas
```

Expected output:

```
hi!
```

## Nested calls

`tests/e2e/nested_calls.bas` combines a side-effecting `SUB` with a string-returning `FUNCTION`.
