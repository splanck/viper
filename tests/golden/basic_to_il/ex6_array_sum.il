il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_input_line() -> str
extern @rt_to_int(str) -> i64
extern @rt_arr_i32_new(i64) -> ptr
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  %t2 = alloca 8
  %t3 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t4 = call @rt_input_line()
  .loc 1 1 4
  %t5 = call @rt_to_int(%t4)
  .loc 1 1 4
  store i64, %t3, %t5
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t6 = load i64, %t3
  .loc 1 2 4
  %t7 = call @rt_arr_i32_new(%t6)
  .loc 1 2 4
  store ptr, %t2, %t7
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  store i64, %t1, 0
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  store i64, %t0, 0
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 4
  br loop_head
L100:
  .loc 1 10 11
  %t27 = load i64, %t0
  .loc 1 10 5
  call @rt_print_i64(%t27)
  .loc 1 10 5
  %t28 = const_str @.L0
  .loc 1 10 5
  call @rt_print_str(%t28)
  .loc 1 10 5
  br L110
L110:
  .loc 1 11 5
  br exit
exit:
  ret 0
loop_head:
  .loc 1 5 10
  %t8 = load i64, %t1
  .loc 1 5 14
  %t9 = load i64, %t3
  .loc 1 5 12
  %t10 = scmp_lt %t8, %t9
  .loc 1 5 4
  cbr %t10, loop_body, done
loop_body:
  .loc 1 6 17
  %t11 = load i64, %t1
  .loc 1 6 21
  %t12 = load i64, %t1
  .loc 1 6 19
  %t13 = mul %t11, %t12
  .loc 1 6 19
  %t14 = load ptr, %t2
  .loc 1 6 12
  %t15 = load i64, %t1
  .loc 1 6 10
  %t16 = shl %t15, 3
  .loc 1 6 10
  %t17 = gep %t14, %t16
  .loc 1 6 6
  store i64, %t17, %t13
  .loc 1 7 14
  %t18 = load i64, %t0
  .loc 1 7 18
  %t19 = load ptr, %t2
  .loc 1 7 20
  %t20 = load i64, %t1
  .loc 1 7 18
  %t21 = shl %t20, 3
  .loc 1 7 18
  %t22 = gep %t19, %t21
  .loc 1 7 18
  %t23 = load i64, %t22
  .loc 1 7 16
  %t24 = add %t18, %t23
  .loc 1 7 6
  store i64, %t0, %t24
  .loc 1 8 14
  %t25 = load i64, %t1
  .loc 1 8 16
  %t26 = add %t25, 1
  .loc 1 8 6
  store i64, %t1, %t26
  .loc 1 5 4
  br loop_head
done:
  .loc 1 5 4
  br L100
}
