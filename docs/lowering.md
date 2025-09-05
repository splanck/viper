#Lowering Reference

| BASIC         | IL runtime call |
|---------------|-----------------|
| `SQR(x)`      | `call @rt_sqrt(x)` |
| `ABS(i)`      | `call @rt_abs_i64(i)` |
| `ABS(x#)`     | `call @rt_abs_f64(x)` |
| `FLOOR(x)`    | `call @rt_floor(x)` |
| `CEIL(x)`     | `call @rt_ceil(x)` |
| `F(x)`        | `call <retTy> @F(args…)` |
| `S(x$, a())`  | `call void @S(str, ptr)` |

Integer arguments to `SQR`, `FLOOR`, and `CEIL` are first widened to `f64`.

User-defined `FUNCTION` and `SUB` calls lower to direct `call` instructions.
Arguments are evaluated left-to-right and converted to the callee's expected
types:

- When a parameter expects `f64` but receives `i64`, a `sitofp` widening is
  inserted.
- `str` parameters receive their handles directly.
- Array parameters (`i64[]` or `str[]`) pass the pointer/handle without any
  load.

## Procedure Definitions

Each BASIC `FUNCTION` or `SUB` becomes an IL function named `@<name>`. The
entry block label is deterministically `entry_<name>` and a closing block
`ret_<name>` carries the fallthrough `ret`. Scalar parameters are
materialized by allocating stack slots and storing the incoming values. Array
parameters (`i64[]` or `str[]`) are passed as pointers/handles and stored
directly without copying.
