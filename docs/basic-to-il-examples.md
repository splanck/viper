# BASIC to IL Examples

Below are three small BASIC programs and their complete IL equivalents. Each IL module is self-contained and conforms to the IL v0.1 spec (textual format, strict terminators, typed loads/stores, runtime calls for I/O and strings).

## Legend

- Locals use `alloca 8` with `load`/`store`.
- String literals become `global const str @.Lk = "..."` then `const_str @.Lk`.
- Conditions use `scmp_*`, `icmp_*`, or `fcmp_*` producing `i1`.
- Branches are explicit: `br`/`cbr`.
- Printing calls `@rt_print_str`, `@rt_print_i64`, or `@rt_print_f64`.

## Example 1 — Hello, arithmetic, and a conditional branch

### BASIC

```basic
10 PRINT "HELLO"
20 LET X = 2 + 3
30 LET Y = X * 2
35 PRINT "READY"
40 PRINT Y
50 IF Y > 8 THEN GOTO 80
60 PRINT 4
70 GOTO 90
80 PRINT Y
90 END
```

### IL

```il
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
global const str @.L0 = "HELLO"
global const str @.L1 = "READY"
func @main() -> i32 {
entry:
  ; locals
  %x_slot = alloca 8
  %y_slot = alloca 8
  ; X = 2 + 3
  %t0 = add 2, 3
  store i64, %x_slot, %t0
  ; Y = X * 2
  %xv = load i64, %x_slot
  %t1 = mul %xv, 2
  store i64, %y_slot, %t1
  ; PRINT "HELLO"
  %s0 = const_str @.L0
  call @rt_print_str(%s0)
  ; PRINT "READY"
  %s1 = const_str @.L1
  call @rt_print_str(%s1)
  ; PRINT Y
  %yv0 = load i64, %y_slot
  call @rt_print_i64(%yv0)
  ; IF Y > 8 THEN GOTO 80 ELSE fallthrough
  %yv1 = load i64, %y_slot
  %cond = scmp_gt %yv1, 8
  cbr %cond, label then80, label else60
else60:
  ; PRINT 4
  call @rt_print_i64(4)
  br label after
then80:
  ; PRINT Y
  %yv2 = load i64, %y_slot
  call @rt_print_i64(%yv2)
  br label after
after:
  ret 0
}
```

## Example 2 — Sum 1..10 with a WHILE loop

### BASIC

```basic
10 PRINT "SUM 1..10"
20 LET I = 1
30 LET S = 0
40 WHILE I <= 10
50   LET S = S + I
60   LET I = I + 1
70 WEND
80 PRINT S
90 PRINT "DONE"
100 END
```

### IL

```il
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
global const str @.L0 = "SUM 1..10"
global const str @.L1 = "DONE"
func @main() -> i32 {
entry:
  %i_slot = alloca 8
  %s_slot = alloca 8
  ; PRINT "SUM 1..10"
  %h = const_str @.L0
  call @rt_print_str(%h)
  ; I = 1, S = 0
  store i64, %i_slot, 1
  store i64, %s_slot, 0
  br label loop_head
loop_head:
  %i0 = load i64, %i_slot
  %c = scmp_le %i0, 10
  cbr %c, label loop_body, label done
loop_body:
  %s0 = load i64, %s_slot
  %s1 = add %s0, %i0
  store i64, %s_slot, %s1
  %i1 = add %i0, 1
  store i64, %i_slot, %i1
  br label loop_head
done:
  %s2 = load i64, %s_slot
  call @rt_print_i64(%s2)
  %d = const_str @.L1
  call @rt_print_str(%d)
  ret 0
}
```

## Example 3 — Nested loops: 5×5 multiplication table

### BASIC

```basic
10 PRINT "TABLE 5x5"
20 LET N = 5
30 LET I = 1
40 WHILE I <= N
50   LET J = 1
60   WHILE J <= N
70     PRINT I * J
80     LET J = J + 1
90   WEND
100  LET I = I + 1
110 WEND
120 END
```

### IL

```il
il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
global const str @.L0 = "TABLE 5x5"
func @main() -> i32 {
entry:
  %n_slot = alloca 8
  %i_slot = alloca 8
  %j_slot = alloca 8
  ; heading
  %h = const_str @.L0
  call @rt_print_str(%h)
  ; N=5, I=1
  store i64, %n_slot, 5
  store i64, %i_slot, 1
  br label outer_head
outer_head:
  %i0 = load i64, %i_slot
  %n0 = load i64, %n_slot
  %oc = scmp_le %i0, %n0
  cbr %oc, label outer_body, label outer_done
outer_body:
  ; J=1
  store i64, %j_slot, 1
  br label inner_head
inner_head:
  %j0 = load i64, %j_slot
  %n1 = load i64, %n_slot
  %ic = scmp_le %j0, %n1
  cbr %ic, label inner_body, label inner_done
inner_body:
  %i1 = load i64, %i_slot
  %prod = mul %i1, %j0
  call @rt_print_i64(%prod)
  %j1 = add %j0, 1
  store i64, %j_slot, %j1
  br label inner_head
inner_done:
  %i2 = add %i0, 1
  store i64, %i_slot, %i2
  br label outer_head
outer_done:
  ret 0
}
```

