Below are six small BASIC programs (≈10–20 lines each) and their complete IL equivalents. Each IL module is self‑contained and conforms to the IL v0.1 spec we drafted (textual format, strict terminators, typed loads/stores, runtime calls for I/O & strings).

	Legend used below
		○ Locals are modeled with alloca 8 + load/store.
		○ String literals use global const str @.Lk = "..." then const_str @.Lk.
		○ Conditions use scmp_* / icmp_* / fcmp_* producing i1.
		○ Branches always explicit: br / cbr.
		○ Printing uses runtime: @rt_print_str, @rt_print_i64, @rt_print_f64.

Example 1 — Hello, arithmetic, and conditional branch
BASIC (10 lines)

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
IL

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
Notes:
	• LET lowers to alloca + store; reads use load.
	• The GOTO 80 maps to br label then80.

Example 2 — Sum 1..10 with a WHILE loop
BASIC (10 lines)

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
IL

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
  %c  = scmp_le %i0, 10
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
Notes:
	• Classic while lowering: entry → loop_head (cbr) → loop_body → loop_head → done.

Example 3 — Nested loops: 5×5 multiplication table
BASIC (12 lines)

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
IL

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
Notes:
	• Nested while translates to two head/body/done trios.
	• Each PRINT is an IL call to the runtime.

Example 4 — Read input and compute factorial
BASIC (11 lines)

10 PRINT "FACTORIAL"
20 PRINT "ENTER N:"
30 LET S$ = INPUT$
40 LET N = VAL(S$)
50 LET R = 1
60 WHILE N > 1
70   LET R = R * N
80   LET N = N - 1
90 WEND
100 PRINT R
110 END
IL

il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_input_line() -> str
extern @rt_to_int(str) -> i64
global const str @.L0 = "FACTORIAL"
global const str @.L1 = "ENTER N:"
func @main() -> i32 {
entry:
  %s_slot = alloca 8    ; str handle
  %n_slot = alloca 8
  %r_slot = alloca 8
; headings
  %h0 = const_str @.L0
  call @rt_print_str(%h0)
  %h1 = const_str @.L1
  call @rt_print_str(%h1)
; S$ = INPUT$
  %line = call @rt_input_line()
  store str, %s_slot, %line
; N = VAL(S$)
  %sval = load str, %s_slot
  %n0 = call @rt_to_int(%sval)
  store i64, %n_slot, %n0
; R = 1
  store i64, %r_slot, 1
br label loop_head
loop_head:
  %n1 = load i64, %n_slot
  %c  = scmp_gt %n1, 1
  cbr %c, label loop_body, label done
loop_body:
  %r0 = load i64, %r_slot
  %r1 = mul %r0, %n1
  store i64, %r_slot, %r1
%n2 = sub %n1, 1
  store i64, %n_slot, %n2
  br label loop_head
done:
  %r2 = load i64, %r_slot
  call @rt_print_i64(%r2)
  ret 0
}
Notes:
	• Input uses rt_input_line; conversion uses rt_to_int.
	• On invalid numeric text, rt_to_int (by spec) may trap; that’s OK for v0.1.

Example 5 — String concat, length, substring, equality
BASIC (10 lines)

10 LET A$ = "JOHN"
20 LET B$ = "DOE"
30 LET C$ = A$ + " "
40 LET C$ = C$ + B$
50 PRINT C$
60 LET L = LEN(C$)
70 PRINT L
80 PRINT MID$(C$, 1, 1)
90 IF C$ = "JOHN DOE" THEN PRINT 1 ELSE PRINT 0
100 END
IL

il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_len(str) -> i64
extern @rt_concat(str, str) -> str
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_eq(str, str) -> i1
global const str @.L0 = "JOHN"
global const str @.L1 = "DOE"
global const str @.L2 = " "
global const str @.L3 = "JOHN DOE"
func @main() -> i32 {
entry:
  %a_slot = alloca 8  ; str
  %b_slot = alloca 8  ; str
  %c_slot = alloca 8  ; str
  %l_slot = alloca 8  ; i64
; A$ = "JOHN", B$ = "DOE"
  %sA = const_str @.L0
  %sB = const_str @.L1
  store str, %a_slot, %sA
  store str, %b_slot, %sB
; C$ = A$ + " "
  %sSp = const_str @.L2
  %a0 = load str, %a_slot
  %c0 = call @rt_concat(%a0, %sSp)
  store str, %c_slot, %c0
; C$ = C$ + B$
  %c1 = load str, %c_slot
  %b0 = load str, %b_slot
  %c2 = call @rt_concat(%c1, %b0)
  store str, %c_slot, %c2
; PRINT C$
  %c3 = load str, %c_slot
  call @rt_print_str(%c3)
; L = LEN(C$) ; PRINT L
  %len = call @rt_len(%c3)
  store i64, %l_slot, %len
  call @rt_print_i64(%len)
; PRINT first char: MID$(C$,1,1) => substr(C$, 0, 1) (front end adjusts 1-based to 0-based)
  %first = call @rt_substr(%c3, 0, 1)
  call @rt_print_str(%first)
; IF C$ == "JOHN DOE" THEN PRINT 1 ELSE PRINT 0
  %target = const_str @.L3
  %eq = call @rt_str_eq(%c3, %target)
  cbr %eq, label then1, label else0
then1:
  call @rt_print_i64(1)
  br label exit
else0:
  call @rt_print_i64(0)
  br label exit
exit:
  ret 0
}
Notes:
	• All string operations are via runtime calls.
	• Equality uses rt_str_eq returning an i1.

Example 6 — Heap “array” via rt_alloc, fill with squares, print average as float
BASIC (13 lines)

10 LET N = 5
20 DIM A(N)          ' conceptual; lowered to a heap block of N * 8 bytes
30 LET I = 0
40 LET SUM = 0
50 WHILE I < N
60   LET A(I) = I * I
70   LET SUM = SUM + A(I)
80   LET I = I + 1
90 WEND
100 LET AVG = SUM / N
110 PRINT AVG         ' print as floating average
120 PRINT "DONE"
130 END
IL

il 0.1
extern @rt_alloc(i64) -> ptr
extern @rt_print_f64(f64) -> void
extern @rt_print_str(str) -> void
global const str @.L0 = "DONE"
func @main() -> i32 {
entry:
  %n_slot   = alloca 8
  %i_slot   = alloca 8
  %sum_slot = alloca 8
  %a_slot   = alloca 8   ; ptr to heap block
; N = 5, I = 0, SUM = 0
  store i64, %n_slot, 5
  store i64, %i_slot, 0
  store i64, %sum_slot, 0
; A = rt_alloc(N * 8)
  %n0 = load i64, %n_slot
  %bytes = mul %n0, 8
  %abase = call @rt_alloc(%bytes)
  store ptr, %a_slot, %abase
br label loop_head
loop_head:
  %i0 = load i64, %i_slot
  %n1 = load i64, %n_slot
  %c  = scmp_lt %i0, %n1
  cbr %c, label loop_body, label done
loop_body:
  ; compute address A + i*8
  %a0 = load ptr, %a_slot
  %off = shl %i0, 3         ; i * 8
  %elem_ptr = gep %a0, %off
; A(i) = i * i
  %sq = mul %i0, %i0
  store i64, %elem_ptr, %sq
; SUM += A(i)
  %val = load i64, %elem_ptr
  %sum0 = load i64, %sum_slot
  %sum1 = add %sum0, %val
  store i64, %sum_slot, %sum1
; i++
  %i1 = add %i0, 1
  store i64, %i_slot, %i1
br label loop_head
done:
  ; AVG = (float)SUM / (float)N
  %sum2 = load i64, %sum_slot
  %n2   = load i64, %n_slot
  %fsum = sitofp %sum2
  %fn   = sitofp %n2
  %avg  = fdiv %fsum, %fn
  call @rt_print_f64(%avg)
; "DONE"
  %d = const_str @.L0
  call @rt_print_str(%d)
ret 0
}
Notes:
	• Arrays are modeled as a heap block (ptr) with manual addressing via gep.
	• Average printed as f64, demonstrating sitofp and fdiv.

How to Use These Examples
	• Golden tests: Keep each BASIC + IL pair together; your front end should emit IL text identical (or equivalent modulo temporary names/labels).
	• VM vs Native: Run IL on the interpreter and compare to native output from the IL→ASM backend.
	• Coverage: These six cover arithmetic, control flow, nested loops, input, strings, heap, and float math.

