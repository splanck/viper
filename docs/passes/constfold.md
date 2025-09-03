# constfold

Folds trivial constant computations at the IL level.

## Folded operations

| Pattern | Replacement | Notes |
| --- | --- | --- |
| `ABS(i64 lit)` | integer absolute value | |
| `ABS(f64 lit)` | `fabs` of literal | |
| `FLOOR(f64 lit)` | `floor` of literal | |
| `CEIL(f64 lit)` | `ceil` of literal | |
| `SQR(f64 lit >= 0)` | `sqrt` of literal | operand must be non-negative |
| `POW(f64 lit, i64 small lit)` | `pow` | exponent must be integer `|exp| <= 16` |
| `SIN(0.0)` | `0.0` | no other trig folding |
| `COS(0.0)` | `1.0` | no other trig folding |

## Caveats

* Only literals are folded; mixed constant/non-constant expressions are ignored.
* Floating point results use C `double` semantics and emit exact literals.
* Trigonometric folding is limited to the zero case to avoid precision surprises.
