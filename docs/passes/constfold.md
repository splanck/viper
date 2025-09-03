# constfold (v1)

Folds literal computations at the IL level.

## Supported folds

| Pattern | Result |
|--------|--------|
| `ABS(i64 lit)` | absolute value as i64 |
| `ABS(f64 lit)` | absolute value as f64 |
| `FLOOR(f64 lit)` | `floor` result |
| `CEIL(f64 lit)` | `ceil` result |
| `SQR(f64 lit ≥ 0)` | `sqrt` result |
| `POW(f64 lit, i64 lit)` *(\|exp\| ≤ 16)* | `pow` result |
| `SIN(0)` | `0` |
| `COS(0)` | `1` |

All floating-point folds use C math semantics and emit exact `f64` literals in the
optimized IL.

## Caveats

* Only the patterns above are folded.
* No general trigonometric folding beyond `SIN(0)` and `COS(0)`.
* `POW` folds only for small integer exponents and `SQR` requires non‑negative inputs.
