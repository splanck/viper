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
  %t41 = load i64, %t0
  .loc 1 10 5
  call @rt_print_i64(%t41)
  .loc 1 10 5
  %t42 = const_str @.L0
  .loc 1 10 5
  call @rt_print_str(%t42)
  .loc 1 10 5
  br L110
L110:
  .loc 1 11 5
  br exit
exit:
  %t43 = load ptr, %t2
  call @rt_arr_i32_release(%t43)
  store ptr, %t2, null
  ret 0
loop_head:
  .loc 1 5 10
  %t9 = load i64, %t1
  .loc 1 5 14
  %t10 = load i64, %t3
  .loc 1 5 12
  %t11 = scmp_lt %t9, %t10
  .loc 1 5 12
  %t12 = zext1 %t11
  .loc 1 5 12
  %t13 = isub.ovf 0, %t12
  .loc 1 5 4
  %t14 = trunc1 %t13
  .loc 1 5 4
  cbr %t14, loop_body, done
loop_body:
  .loc 1 6 17
  %t15 = load i64, %t1
  .loc 1 6 21
  %t16 = load i64, %t1
  .loc 1 6 19
  %t17 = imul.ovf %t15, %t16
  .loc 1 6 19
  %t18 = load ptr, %t2
  .loc 1 6 12
  %t19 = load i64, %t1
  .loc 1 6 10
  %t20 = call @rt_arr_i32_len(%t18)
  .loc 1 6 10
  %t21 = scmp_lt %t19, 0
  .loc 1 6 10
  %t22 = scmp_ge %t19, %t20
  .loc 1 6 10
  %t23 = zext1 %t21
  .loc 1 6 10
  %t24 = zext1 %t22
  .loc 1 6 10
  %t25 = iadd.ovf %t23, %t24
  .loc 1 6 10
  %t26 = scmp_gt %t25, 0
  .loc 1 6 10
  cbr %t26, bc_oob0, bc_ok0
done:
  .loc 1 5 4
  br L100
bc_ok0:
  .loc 1 6 6
  call @rt_arr_i32_set(%t18, %t19, %t17)
  .loc 1 7 14
  %t27 = load i64, %t0
  .loc 1 7 18
  %t28 = load ptr, %t2
  .loc 1 7 20
  %t29 = load i64, %t1
  .loc 1 7 18
  %t30 = call @rt_arr_i32_len(%t28)
  .loc 1 7 18
  %t31 = scmp_lt %t29, 0
  .loc 1 7 18
  %t32 = scmp_ge %t29, %t30
  .loc 1 7 18
  %t33 = zext1 %t31
  .loc 1 7 18
  %t34 = zext1 %t32
  .loc 1 7 18
  %t35 = iadd.ovf %t33, %t34
  .loc 1 7 18
  %t36 = scmp_gt %t35, 0
  .loc 1 7 18
  cbr %t36, bc_oob1, bc_ok1
bc_oob0:
  .loc 1 6 10
  call @rt_arr_oob_panic(%t19, %t20)
  .loc 1 6 10
  trap
bc_ok1:
  .loc 1 7 18
  %t37 = call @rt_arr_i32_get(%t28, %t29)
  .loc 1 7 16
  %t38 = iadd.ovf %t27, %t37
  .loc 1 7 6
  store i64, %t0, %t38
  .loc 1 8 14
  %t39 = load i64, %t1
  .loc 1 8 16
  %t40 = iadd.ovf %t39, 1
  .loc 1 8 6
  store i64, %t1, %t40
  .loc 1 5 4
  br loop_head
bc_oob1:
  .loc 1 7 18
  call @rt_arr_oob_panic(%t29, %t30)
  .loc 1 7 18
  trap
}
