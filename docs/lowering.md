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

Recursive calls lower the same way; see [factorial.bas](examples.md#factorial) for a recursion sanity check.

## Compilation unit lowering

BASIC programs lower procedures first, then wrap remaining top-level statements
into a synthetic `@main` function:

```
Program
├─ procs[] → @<name>
└─ main[]  → @main
```

This ordering guarantees functions are listed before `@main`.

## Procedure Definitions

Each BASIC `FUNCTION` or `SUB` becomes an IL function named `@<name>`. The
entry block label is deterministically `entry_<name>` and a closing block
`ret_<name>` carries the fallthrough `ret`. Scalar parameters are
materialized by allocating stack slots and storing the incoming values. Array
parameters (`i64[]` or `str[]`) are passed as pointers/handles and stored
directly without copying.

### Deterministic label naming (procedures)

Blocks created while lowering a procedure use predictable labels so goldens
stay stable. Within a procedure `proc`, block names follow these patterns:

* `entry_proc` and `ret_proc` for the entry and synthetic return blocks.
* `if_then_k_proc`, `if_else_k_proc`, `if_end_k_proc` for `IF` constructs.
* `while_head_k_proc`, `while_body_k_proc`, `while_end_k_proc` for `WHILE`.
* `for_head_k_proc`, `for_body_k_proc`, `for_inc_k_proc`, `for_end_k_proc` for `FOR`.
* `call_cont_k_proc` for call continuations.

The counter `k` is monotonic **per procedure**; labels never depend on container
iteration order. Rebuilding the same source therefore yields identical label
names across runs.

Example BASIC and corresponding IL excerpt:

```
10 FUNCTION F(X)
20 FOR I = 0 TO 1
30   WHILE X < 10
40     IF X THEN CALL P
50     X = X + 1
60   WEND
70 NEXT I
80 F = X
90 RETURN
100 END FUNCTION
```

```
func @F(i64 %X) -> i64 {
  entry_F:
    br for_head_0_F
  for_head_0_F:
    ...
    cbr %t0 for_body_0_F for_end_0_F
  for_body_0_F:
    br while_head_0_F
  while_head_0_F:
    ...
    cbr %t1 while_body_0_F while_end_0_F
  while_body_0_F:
    cbr %t2 if_then_0_F if_end_0_F
  if_then_0_F:
    call @P()
    br call_cont_0_F
  call_cont_0_F:
    ...
    br while_head_0_F
  if_end_0_F:
    ...
    br while_head_0_F
  while_end_0_F:
    br for_inc_0_F
  for_inc_0_F:
    ...
    br for_head_0_F
  for_end_0_F:
    br ret_F
  ret_F:
    ret %X
}
```

## Return statements

`RETURN` lowers directly to an IL `ret` terminator (or `ret void` for `SUB`).
Once emitted, the current block is considered closed and no further statements
from the same BASIC block are lowered. This prevents generating dead
instructions after a `RETURN` and ensures each block has exactly one
terminator.
