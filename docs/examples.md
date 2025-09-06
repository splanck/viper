#Examples

A few BASIC samples illustrate the runtime.

Top-level BASIC statements are lowered into an IL function named `@main`.

        ##Monte Carlo π

`examples /
        basic /
        monte_carlo_pi.bas` estimates π by sampling points inside the unit square.Run it with :

```sh ilc front basic -
    run examples / basic /
        monte_carlo_pi.bas
```

        ##Random walk

`examples /
        basic / random_walk.bas` performs a 100 step one -
    dimensional random walk with a fixed seed.Run it with :

```sh ilc front basic -
    run examples / basic /
        random_walk.bas
```

        ##Function calls

`tests /
        e2e / func_calls_factorial.bas` computes a factorial via recursive
`FUNCTION` calls. `tests / e2e / func_calls_concat.bas` shows a string -
    returning
`FUNCTION` that concatenates its arguments.

## Factorial

`tests/e2e/factorial.bas` computes `FACT(10)` recursively and prints `3628800`.

## Fibonacci

`tests/e2e/fibonacci.bas` computes `FIB(10)` recursively and prints `55`. It
finishes in roughly 180 VM steps, so a guard of 200 suffices if using
`--max-steps`.

## Nested calls

`tests/e2e/nested_calls.bas` combines a side-effecting `SUB` with a string-returning `FUNCTION`.
