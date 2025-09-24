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
extern @rt_arr_oob_panic(i64, i64) -> void
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  %t2 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t3 = call @rt_arr_i32_new(1)
  .loc 1 1 4
  store ptr, %t2, %t3
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 13
  %t4 = load ptr, %t2
  .loc 1 2 13
  %t5 = call @rt_arr_i32_len(%t4)
  .loc 1 2 13
  %t6 = sub %t5, 1
  .loc 1 2 4
  store i64, %t1, %t6
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 12
  %t7 = load i64, %t1
  .loc 1 3 15
  %t8 = add %t7, 2
  .loc 1 3 4
  %t9 = load ptr, %t2
  .loc 1 3 4
  %t10 = call @rt_arr_i32_resize(%t9, %t8)
  .loc 1 3 4
  store ptr, %t2, %t10
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 16
  %t11 = load ptr, %t2
  .loc 1 4 10
  %t12 = load i64, %t1
  .loc 1 4 8
  %t13 = call @rt_arr_i32_len(%t11)
  .loc 1 4 8
  %t14 = scmp_lt %t12, 0
  .loc 1 4 8
  %t15 = scmp_ge %t12, %t13
  .loc 1 4 8
  %t16 = zext1 %t14
  .loc 1 4 8
  %t17 = zext1 %t15
  .loc 1 4 8
  %t18 = add %t16, %t17
  .loc 1 4 8
  %t19 = scmp_gt %t18, 0
  .loc 1 4 8
  cbr %t19, bc_oob0, bc_ok0
L50:
  .loc 1 5 12
  %t20 = load ptr, %t2
  .loc 1 5 14
  %t21 = load i64, %t1
  .loc 1 5 12
  %t22 = call @rt_arr_i32_len(%t20)
  .loc 1 5 12
  %t23 = scmp_lt %t21, 0
  .loc 1 5 12
  %t24 = scmp_ge %t21, %t22
  .loc 1 5 12
  %t25 = zext1 %t23
  .loc 1 5 12
  %t26 = zext1 %t24
  .loc 1 5 12
  %t27 = add %t25, %t26
  .loc 1 5 12
  %t28 = scmp_gt %t27, 0
  .loc 1 5 12
  cbr %t28, bc_oob1, bc_ok1
L60:
  .loc 1 6 10
  %t30 = load i64, %t1
  .loc 1 6 4
  call @rt_print_i64(%t30)
  .loc 1 6 4
  %t31 = const_str @.L0
  .loc 1 6 4
  call @rt_print_str(%t31)
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 10
  %t32 = load i64, %t0
  .loc 1 7 4
  call @rt_print_i64(%t32)
  .loc 1 7 4
  %t33 = const_str @.L0
  .loc 1 7 4
  call @rt_print_str(%t33)
  .loc 1 7 4
  br L80
L80:
  .loc 1 8 4
  br exit
exit:
  ret 0
bc_ok0:
  .loc 1 4 4
  call @rt_arr_i32_set(%t11, %t12, 7)
  .loc 1 4 4
  br L50
bc_oob0:
  .loc 1 4 8
  call @rt_arr_oob_panic(%t12, %t13)
  .loc 1 4 8
  trap
bc_ok1:
  .loc 1 5 12
  %t29 = call @rt_arr_i32_get(%t20, %t21)
  .loc 1 5 4
  store i64, %t0, %t29
  .loc 1 5 4
  br L60
bc_oob1:
  .loc 1 5 12
  call @rt_arr_oob_panic(%t21, %t22)
  .loc 1 5 12
  trap
}
