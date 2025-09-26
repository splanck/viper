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
extern @rt_arr_i32_retain(ptr) -> void
extern @rt_arr_i32_release(ptr) -> void
extern @rt_arr_oob_panic(i64, i64) -> void
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  %t2 = alloca 8
  store ptr, %t2, null
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
  call @rt_arr_i32_retain(%t7)
  .loc 1 2 4
  %t8 = load ptr, %t2
  .loc 1 2 4
  call @rt_arr_i32_release(%t8)
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
  %t38 = load i64, %t0
  .loc 1 10 5
  call @rt_print_i64(%t38)
  .loc 1 10 5
  %t39 = const_str @.L0
  .loc 1 10 5
  call @rt_print_str(%t39)
  .loc 1 10 5
  br L110
L110:
  .loc 1 11 5
  br exit
exit:
  %t40 = load ptr, %t2
  call @rt_arr_i32_release(%t40)
  store ptr, %t2, null
  ret 0
loop_head:
  .loc 1 5 10
  %t9 = load i64, %t1
  .loc 1 5 14
  %t10 = load i64, %t3
  .loc 1 5 12
  %t11 = scmp_lt %t9, %t10
  .loc 1 5 4
  cbr %t11, loop_body, done
loop_body:
  .loc 1 6 17
  %t12 = load i64, %t1
  .loc 1 6 21
  %t13 = load i64, %t1
  .loc 1 6 19
  %t14 = imul.ovf %t12, %t13
  .loc 1 6 19
  %t15 = load ptr, %t2
  .loc 1 6 12
  %t16 = load i64, %t1
  .loc 1 6 10
  %t17 = call @rt_arr_i32_len(%t15)
  .loc 1 6 10
  %t18 = scmp_lt %t16, 0
  .loc 1 6 10
  %t19 = scmp_ge %t16, %t17
  .loc 1 6 10
  %t20 = zext1 %t18
  .loc 1 6 10
  %t21 = zext1 %t19
  .loc 1 6 10
  %t22 = iadd.ovf %t20, %t21
  .loc 1 6 10
  %t23 = scmp_gt %t22, 0
  .loc 1 6 10
  cbr %t23, bc_oob0, bc_ok0
done:
  .loc 1 5 4
  br L100
bc_ok0:
  .loc 1 6 6
  call @rt_arr_i32_set(%t15, %t16, %t14)
  .loc 1 7 14
  %t24 = load i64, %t0
  .loc 1 7 18
  %t25 = load ptr, %t2
  .loc 1 7 20
  %t26 = load i64, %t1
  .loc 1 7 18
  %t27 = call @rt_arr_i32_len(%t25)
  .loc 1 7 18
  %t28 = scmp_lt %t26, 0
  .loc 1 7 18
  %t29 = scmp_ge %t26, %t27
  .loc 1 7 18
  %t30 = zext1 %t28
  .loc 1 7 18
  %t31 = zext1 %t29
  .loc 1 7 18
  %t32 = iadd.ovf %t30, %t31
  .loc 1 7 18
  %t33 = scmp_gt %t32, 0
  .loc 1 7 18
  cbr %t33, bc_oob1, bc_ok1
bc_oob0:
  .loc 1 6 10
  call @rt_arr_oob_panic(%t16, %t17)
  .loc 1 6 10
  trap
bc_ok1:
  .loc 1 7 18
  %t34 = call @rt_arr_i32_get(%t25, %t26)
  .loc 1 7 16
  %t35 = iadd.ovf %t24, %t34
  .loc 1 7 6
  store i64, %t0, %t35
  .loc 1 8 14
  %t36 = load i64, %t1
  .loc 1 8 16
  %t37 = iadd.ovf %t36, 1
  .loc 1 8 6
  store i64, %t1, %t37
  .loc 1 5 4
  br loop_head
bc_oob1:
  .loc 1 7 18
  call @rt_arr_oob_panic(%t26, %t27)
  .loc 1 7 18
  trap
}
