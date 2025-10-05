il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_resize(ptr, i64) -> ptr
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
  br L10
L10:
  .loc 1 1 4
  %t3 = call @rt_arr_i32_new(1)
  .loc 1 1 4
  call @rt_arr_i32_retain(%t3)
  .loc 1 1 4
  %t4 = load ptr, %t2
  .loc 1 1 4
  call @rt_arr_i32_release(%t4)
  .loc 1 1 4
  store ptr, %t2, %t3
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 13
  %t5 = load ptr, %t2
  .loc 1 2 13
  %t6 = call @rt_arr_i32_len(%t5)
  .loc 1 2 13
  %t7 = isub.ovf %t6, 1
  .loc 1 2 4
  store i64, %t1, %t7
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 12
  %t8 = load i64, %t1
  .loc 1 3 15
  %t9 = iadd.ovf %t8, 2
  .loc 1 3 4
  %t10 = load ptr, %t2
  .loc 1 3 4
  %t11 = call @rt_arr_i32_resize(%t10, %t9)
  .loc 1 3 4
  call @rt_arr_i32_retain(%t11)
  .loc 1 3 4
  %t12 = load ptr, %t2
  .loc 1 3 4
  call @rt_arr_i32_release(%t12)
  .loc 1 3 4
  store ptr, %t2, %t11
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 16
  %t13 = load ptr, %t2
  .loc 1 4 10
  %t14 = load i64, %t1
  .loc 1 4 8
  %t15 = call @rt_arr_i32_len(%t13)
  .loc 1 4 8
  %t16 = scmp_lt %t14, 0
  .loc 1 4 8
  %t17 = scmp_ge %t14, %t15
  .loc 1 4 8
  %t18 = zext1 %t16
  .loc 1 4 8
  %t19 = zext1 %t17
  .loc 1 4 8
  %t20 = or %t18, %t19
  .loc 1 4 8
  %t21 = icmp_ne %t20, 0
  .loc 1 4 8
  cbr %t21, bc_oob0, bc_ok0
L50:
  .loc 1 5 12
  %t22 = load ptr, %t2
  .loc 1 5 14
  %t23 = load i64, %t1
  .loc 1 5 12
  %t24 = call @rt_arr_i32_len(%t22)
  .loc 1 5 12
  %t25 = scmp_lt %t23, 0
  .loc 1 5 12
  %t26 = scmp_ge %t23, %t24
  .loc 1 5 12
  %t27 = zext1 %t25
  .loc 1 5 12
  %t28 = zext1 %t26
  .loc 1 5 12
  %t29 = or %t27, %t28
  .loc 1 5 12
  %t30 = icmp_ne %t29, 0
  .loc 1 5 12
  cbr %t30, bc_oob1, bc_ok1
L60:
  .loc 1 6 10
  %t32 = load i64, %t1
  .loc 1 6 4
  call @rt_print_i64(%t32)
  .loc 1 6 4
  %t33 = const_str @.L0
  .loc 1 6 4
  call @rt_print_str(%t33)
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 10
  %t34 = load i64, %t0
  .loc 1 7 4
  call @rt_print_i64(%t34)
  .loc 1 7 4
  %t35 = const_str @.L0
  .loc 1 7 4
  call @rt_print_str(%t35)
  .loc 1 7 4
  br L80
L80:
  .loc 1 8 4
  br exit
exit:
  %t36 = load ptr, %t2
  call @rt_arr_i32_release(%t36)
  store ptr, %t2, null
  ret 0
bc_ok0:
  .loc 1 4 4
  call @rt_arr_i32_set(%t13, %t14, 7)
  .loc 1 4 4
  br L50
bc_oob0:
  .loc 1 4 8
  call @rt_arr_oob_panic(%t14, %t15)
  .loc 1 4 8
  trap
bc_ok1:
  .loc 1 5 12
  %t31 = call @rt_arr_i32_get(%t22, %t23)
  .loc 1 5 4
  store i64, %t0, %t31
  .loc 1 5 4
  br L60
bc_oob1:
  .loc 1 5 12
  call @rt_arr_oob_panic(%t23, %t24)
  .loc 1 5 12
  trap
}
