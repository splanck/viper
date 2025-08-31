il 0.1
extern @rt_alloc(i64) -> ptr
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_input_line() -> str
extern @rt_to_int(str) -> i64
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  %t2 = alloca 8
  %t3 = alloca 8
  br label L10
L10:
  .loc 1 1 4
  %t4 = call @rt_input_line()
  .loc 1 1 4
  %t5 = call @rt_to_int(%t4)
  .loc 1 1 4
  store i64, %t3, %t5
  .loc 1 1 4
  br label L20
L20:
  .loc 1 2 10
  %t6 = load i64, %t3
  .loc 1 2 4
  %t7 = mul %t6, 8
  .loc 1 2 4
  %t8 = call @rt_alloc(%t7)
  .loc 1 2 4
  store ptr, %t2, %t8
  .loc 1 2 4
  br label L30
L30:
  .loc 1 3 4
  store i64, %t1, 0
  .loc 1 3 4
  br label L40
L40:
  .loc 1 4 4
  store i64, %t0, 0
  .loc 1 4 4
  br label L50
L50:
  .loc 1 5 4
  br label loop_head
L100:
  .loc 1 10 11
  %t28 = load i64, %t0
  .loc 1 10 5
  call @rt_print_i64(%t28)
  .loc 1 10 5
  br label L110
L110:
  .loc 1 11 5
  br label exit
exit:
  ret 0
loop_head:
  .loc 1 5 10
  %t9 = load i64, %t1
  .loc 1 5 14
  %t10 = load i64, %t3
  .loc 1 5 12
  %t11 = scmp_lt %t9, %t10
  .loc 1 5 4
  cbr %t11, label loop_body, label done
loop_body:
  .loc 1 6 17
  %t12 = load i64, %t1
  .loc 1 6 21
  %t13 = load i64, %t1
  .loc 1 6 19
  %t14 = mul %t12, %t13
  .loc 1 6 12
  %t15 = load i64, %t1
  .loc 1 6 6
  %t16 = load ptr, %t2
  .loc 1 6 6
  %t17 = shl %t15, 3
  .loc 1 6 6
  %t18 = gep %t16, %t17
  .loc 1 6 6
  store i64, %t18, %t14
  .loc 1 7 14
  %t19 = load i64, %t0
  .loc 1 7 18
  %t20 = load ptr, %t2
  .loc 1 7 20
  %t21 = load i64, %t1
  .loc 1 7 18
  %t22 = shl %t21, 3
  .loc 1 7 18
  %t23 = gep %t20, %t22
  .loc 1 7 18
  %t24 = load i64, %t23
  .loc 1 7 16
  %t25 = add %t19, %t24
  .loc 1 7 6
  store i64, %t0, %t25
  .loc 1 8 14
  %t26 = load i64, %t1
  .loc 1 8 16
  %t27 = add %t26, 1
  .loc 1 8 6
  store i64, %t1, %t27
  .loc 1 5 4
  br label loop_head
done:
  .loc 1 5 4
  br label L100
}
