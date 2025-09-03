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
