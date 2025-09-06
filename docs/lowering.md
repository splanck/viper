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

### Deterministic label naming (procedures)

Within a procedure, block labels incorporate the procedure name and a
monotonic counter to keep IL stable across runs. Common shapes follow the
scheme:

- `entry_<proc>`, `ret_<proc>`
- `if_then_<k>_<proc>`, `if_else_<k>_<proc>`, `if_end_<k>_<proc>`
- `while_head_<k>_<proc>`, `while_body_<k>_<proc>`, `while_end_<k>_<proc>`
- `for_head_<k>_<proc>`, `for_body_<k>_<proc>`, `for_inc_<k>_<proc>`,
  `for_end_<k>_<proc>`

Example:

```
FUNCTION F(N)
IF N THEN
  F = 1
ELSE
  F = 2
END IF
END FUNCTION
```

Lowers to (truncated):

```
func @F(i64 %N) -> i64 {
entry_F:
  ...
  cbr %t0, if_then_0_F, if_else_0_F
if_then_0_F:
  ...
  br if_end_0_F
if_else_0_F:
  ...
  br if_end_0_F
if_end_0_F:
  br ret_F
ret_F:
  ret 0
}
```

Deterministic names prevent golden test churn and simplify diffing.
