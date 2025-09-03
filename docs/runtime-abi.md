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

The runtime exposes a reproducible pseudo‑random number generator based on a
64‑bit linear congruential generator:

```
state = state * 6364136223846793005 + 1
```

`rt_rnd` returns the top 53 bits of `state` scaled by `2^-53`, yielding a
double in \[0,1). The generator is seeded via `rt_randomize_u64` or
`rt_randomize_i64`, which set the internal `state` exactly (the signed variant
casts to `uint64_t`). The initial state defaults to `0x0123456789ABCDEF` and is
non‑zero so that calling `rt_rnd` without seeding is deterministic. Seeding with
the same value guarantees the same sequence across platforms.

| Symbol | Signature | Semantics |
|--------|-----------|-----------|
| `@rt_randomize_u64` | `i64 -> void` | set RNG state exactly |
| `@rt_randomize_i64` | `i64 -> void` | set RNG state from signed value |
| `@rt_rnd` | ` -> f64` | next uniform double in \[0,1) |
