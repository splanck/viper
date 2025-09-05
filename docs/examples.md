#Examples

A few BASIC samples illustrate the runtime.

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
