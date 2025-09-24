il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_input_line() -> str
extern @rt_to_int(str) -> i64
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_len(ptr) -> i64
extern @rt_arr_i32_get(ptr, i64) -> i64
extern @rt_arr_i32_set(ptr, i64, i64) -> void
extern @rt_arr_oob_panic(i64, i64) -> void
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
  %t37 = load i64, %t0
  .loc 1 10 5
  call @rt_print_i64(%t37)
  .loc 1 10 5
  %t38 = const_str @.L0
  .loc 1 10 5
  call @rt_print_str(%t38)
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
  %t16 = call @rt_arr_i32_len(%t14)
  .loc 1 6 10
  %t17 = scmp_lt %t15, 0
  .loc 1 6 10
  %t18 = scmp_ge %t15, %t16
  .loc 1 6 10
  %t19 = zext1 %t17
  .loc 1 6 10
  %t20 = zext1 %t18
  .loc 1 6 10
  %t21 = add %t19, %t20
  .loc 1 6 10
  %t22 = scmp_gt %t21, 0
  .loc 1 6 10
  cbr %t22, bc_oob0, bc_ok0
done:
  .loc 1 5 4
  br L100
bc_ok0:
  .loc 1 6 6
  call @rt_arr_i32_set(%t14, %t15, %t13)
  .loc 1 7 14
  %t23 = load i64, %t0
  .loc 1 7 18
  %t24 = load ptr, %t2
  .loc 1 7 20
  %t25 = load i64, %t1
  .loc 1 7 18
  %t26 = call @rt_arr_i32_len(%t24)
  .loc 1 7 18
  %t27 = scmp_lt %t25, 0
  .loc 1 7 18
  %t28 = scmp_ge %t25, %t26
  .loc 1 7 18
  %t29 = zext1 %t27
  .loc 1 7 18
  %t30 = zext1 %t28
  .loc 1 7 18
  %t31 = add %t29, %t30
  .loc 1 7 18
  %t32 = scmp_gt %t31, 0
  .loc 1 7 18
  cbr %t32, bc_oob1, bc_ok1
bc_oob0:
  .loc 1 6 10
  call @rt_arr_oob_panic(%t15, %t16)
  .loc 1 6 10
  trap
bc_ok1:
  .loc 1 7 18
  %t33 = call @rt_arr_i32_get(%t24, %t25)
  .loc 1 7 16
  %t34 = add %t23, %t33
  .loc 1 7 6
  store i64, %t0, %t34
  .loc 1 8 14
  %t35 = load i64, %t1
  .loc 1 8 16
  %t36 = add %t35, 1
  .loc 1 8 6
  store i64, %t1, %t36
  .loc 1 5 4
  br loop_head
bc_oob1:
  .loc 1 7 18
  call @rt_arr_oob_panic(%t25, %t26)
  .loc 1 7 18
  trap
}
