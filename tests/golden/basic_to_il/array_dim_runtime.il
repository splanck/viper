il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_resize(ptr, i64) -> ptr
extern @rt_arr_i32_len(ptr) -> i64
extern @rt_arr_i32_get(ptr, i64) -> i64
extern @rt_arr_i32_set(ptr, i64, i64) -> void
extern @rt_arr_i32_retain(ptr) -> void
extern @rt_arr_i32_release(ptr) -> void
extern @rt_arr_oob_panic(i64, i64) -> void
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  %t1 = alloca 8
  %t2 = alloca 8
  store ptr, %t2, null
  br L10
L10:
  .loc 1 1 4
  %t3 = iadd.ovf 1, 1
  .loc 1 1 4
  %t4 = scmp_lt %t3, 0
  .loc 1 1 4
  cbr %t4, dim_len_fail, dim_len_cont
L20:
  .loc 1 2 13
  %t7 = load ptr, %t2
  .loc 1 2 13
  %t8 = call @rt_arr_i32_len(%t7)
  .loc 1 2 13
  %t9 = isub.ovf %t8, 1
  .loc 1 2 4
  store i64, %t1, %t9
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 12
  %t10 = load i64, %t1
  .loc 1 3 15
  %t11 = iadd.ovf %t10, 2
  .loc 1 3 4
  %t12 = iadd.ovf %t11, 1
  .loc 1 3 4
  %t13 = scmp_lt %t12, 0
  .loc 1 3 4
  cbr %t13, redim_len_fail, redim_len_cont
L40:
  .loc 1 4 4
  %t17 = load ptr, %t2
  .loc 1 4 10
  %t18 = load i64, %t1
  .loc 1 4 8
  %t19 = call @rt_arr_i32_len(%t17)
  .loc 1 4 8
  %t20 = scmp_lt %t18, 0
  .loc 1 4 8
  %t21 = scmp_ge %t18, %t19
  .loc 1 4 8
  %t22 = zext1 %t20
  .loc 1 4 8
  %t23 = zext1 %t21
  .loc 1 4 8
  %t24 = or %t22, %t23
  .loc 1 4 8
  %t25 = icmp_ne %t24, 0
  .loc 1 4 8
  cbr %t25, bc_oob0, bc_ok0
L50:
  .loc 1 5 12
  %t26 = load ptr, %t2
  .loc 1 5 14
  %t27 = load i64, %t1
  .loc 1 5 12
  %t28 = call @rt_arr_i32_len(%t26)
  .loc 1 5 12
  %t29 = scmp_lt %t27, 0
  .loc 1 5 12
  %t30 = scmp_ge %t27, %t28
  .loc 1 5 12
  %t31 = zext1 %t29
  .loc 1 5 12
  %t32 = zext1 %t30
  .loc 1 5 12
  %t33 = or %t31, %t32
  .loc 1 5 12
  %t34 = icmp_ne %t33, 0
  .loc 1 5 12
  cbr %t34, bc_oob1, bc_ok1
L60:
  .loc 1 6 10
  %t36 = load i64, %t1
  .loc 1 6 10
  call @rt_print_i64(%t36)
  .loc 1 6 10
  %t37 = const_str @.L0
  .loc 1 6 10
  call @rt_print_str(%t37)
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 10
  %t38 = load i64, %t0
  .loc 1 7 10
  call @rt_print_i64(%t38)
  .loc 1 7 10
  %t39 = const_str @.L0
  .loc 1 7 10
  call @rt_print_str(%t39)
  .loc 1 7 4
  br L80
L80:
  .loc 1 8 4
  ret 0
exit:
  %t40 = load ptr, %t2
  call @rt_arr_i32_release(%t40)
  store ptr, %t2, null
  ret 0
dim_len_fail:
  .loc 1 1 4
  trap
dim_len_cont:
  .loc 1 1 10
  %t5 = call @rt_arr_i32_new(%t3)
  .loc 1 1 10
  call @rt_arr_i32_retain(%t5)
  .loc 1 1 10
  %t6 = load ptr, %t2
  .loc 1 1 10
  call @rt_arr_i32_release(%t6)
  .loc 1 1 10
  store ptr, %t2, %t5
  .loc 1 1 4
  br L20
redim_len_fail:
  .loc 1 3 4
  trap
redim_len_cont:
  .loc 1 3 15
  %t14 = load ptr, %t2
  .loc 1 3 15
  %t15 = call @rt_arr_i32_resize(%t14, %t12)
  .loc 1 3 15
  call @rt_arr_i32_retain(%t15)
  .loc 1 3 15
  %t16 = load ptr, %t2
  .loc 1 3 15
  call @rt_arr_i32_release(%t16)
  .loc 1 3 15
  store ptr, %t2, %t15
  .loc 1 3 4
  br L40
bc_ok0:
  .loc 1 4 8
  call @rt_arr_i32_set(%t17, %t18, 7)
  .loc 1 4 4
  br L50
bc_oob0:
  .loc 1 4 8
  call @rt_arr_oob_panic(%t18, %t19)
  .loc 1 4 8
  trap
bc_ok1:
  .loc 1 5 12
  %t35 = call @rt_arr_i32_get(%t26, %t27)
  .loc 1 5 4
  store i64, %t0, %t35
  .loc 1 5 4
  br L60
bc_oob1:
  .loc 1 5 12
  call @rt_arr_oob_panic(%t27, %t28)
  .loc 1 5 12
  trap
}
