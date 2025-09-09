# Runtime ABI

This document describes the stable C ABI provided by the runtime library.

## Math

| Symbol | Signature | Semantics |
|--------|-----------|-----------|
| `@rt_sqrt` | `f64 -> f64` | square root |
| `@rt_floor` | `f64 -> f64` | floor |
| `@rt_ceil` | `f64 -> f64` | ceiling |
| `@rt_sin` | `f64 -> f64` | sine |
| `@rt_cos` | `f64 -> f64` | cosine |
| `@rt_pow` | `f64, f64 -> f64` | power |
| `@rt_abs_i64` | `i64 -> i64` | absolute value (integer) |
| `@rt_abs_f64` | `f64 -> f64` | absolute value (float) |

## Random

The runtime exposes a simple deterministic 64-bit linear congruential generator:

```
state = state * 6364136223846793005 + 1
```

`@rt_randomize_u64` and `@rt_randomize_i64` set the internal state exactly. The
default state on startup is `0xDEADBEEFCAFEBABE`.

`@rt_rnd` returns a `f64` uniformly distributed in `[0,1)` by taking the top
53 bits of the state and scaling by `2^-53`. For a given seed the sequence of
values is reproducible across platforms.
