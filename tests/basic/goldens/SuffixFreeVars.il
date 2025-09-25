il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_len(ptr) -> i64
extern @rt_arr_i32_get(ptr, i64) -> i64
extern @rt_arr_i32_set(ptr, i64, i64) -> void
extern @rt_arr_i32_retain(ptr) -> void
extern @rt_arr_i32_release(ptr) -> void
extern @rt_arr_oob_panic(i64, i64) -> void
global const str @.L0 = "ok"
global const str @.L1 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  store ptr, %t0, null
  %t1 = alloca 8
  %t2 = alloca 8
  br L08
L0:
L01:
L02:
L03:
L04:
L05:
L06:
L07:
L08:
  .loc 1 1 1
  br L08
  .loc 1 3 1
  %t3 = call @rt_arr_i32_new(2)
  .loc 1 3 1
  call @rt_arr_i32_retain(%t3)
  .loc 1 3 1
  %t4 = load ptr, %t0
  .loc 1 3 1
  call @rt_arr_i32_release(%t4)
  .loc 1 3 1
  store ptr, %t0, %t3
  .loc 1 4 15
  %t5 = const_str @.L0
  .loc 1 4 1
  store str, %t2, %t5
  .loc 1 5 1
  store i64, %t1, 5
  .loc 1 6 17
  %t6 = load i64, %t1
  .loc 1 6 17
  %t7 = load ptr, %t0
  .loc 1 6 5
  %t8 = call @rt_arr_i32_len(%t7)
  .loc 1 6 5
  %t9 = scmp_lt 0, 0
  .loc 1 6 5
  %t10 = scmp_ge 0, %t8
  .loc 1 6 5
  %t11 = zext1 %t9
  .loc 1 6 5
  %t12 = zext1 %t10
  .loc 1 6 5
  %t13 = add %t11, %t12
  .loc 1 6 5
  %t14 = scmp_gt %t13, 0
  .loc 1 6 5
  cbr %t14, bc_oob0, bc_ok0
  .loc 1 7 7
  %t15 = load str, %t2
  .loc 1 7 1
  call @rt_print_str(%t15)
  .loc 1 7 1
  %t16 = const_str @.L1
  .loc 1 7 1
  call @rt_print_str(%t16)
  .loc 1 8 7
  %t17 = load i64, %t1
  .loc 1 8 1
  call @rt_print_i64(%t17)
  .loc 1 8 1
  %t18 = const_str @.L1
  .loc 1 8 1
  call @rt_print_str(%t18)
  .loc 1 9 7
  %t19 = load ptr, %t0
  .loc 1 9 7
  %t20 = call @rt_arr_i32_len(%t19)
  .loc 1 9 7
  %t21 = scmp_lt 0, 0
  .loc 1 9 7
  %t22 = scmp_ge 0, %t20
  .loc 1 9 7
  %t23 = zext1 %t21
  .loc 1 9 7
  %t24 = zext1 %t22
  .loc 1 9 7
  %t25 = add %t23, %t24
  .loc 1 9 7
  %t26 = scmp_gt %t25, 0
  .loc 1 9 7
  cbr %t26, bc_oob1, bc_ok1
exit:
  %t29 = load ptr, %t0
  call @rt_arr_i32_release(%t29)
  store ptr, %t0, null
  ret 0
bc_ok0:
  .loc 1 6 1
  call @rt_arr_i32_set(%t7, 0, %t6)
  .loc 1 6 1
  br L08
bc_oob0:
  .loc 1 6 5
  call @rt_arr_oob_panic(0, %t8)
  .loc 1 6 5
  trap
bc_ok1:
  .loc 1 9 7
  %t27 = call @rt_arr_i32_get(%t19, 0)
  .loc 1 9 1
  call @rt_print_i64(%t27)
  .loc 1 9 1
  %t28 = const_str @.L1
  .loc 1 9 1
  call @rt_print_str(%t28)
  .loc 1 9 1
  br exit
bc_oob1:
  .loc 1 9 7
  call @rt_arr_oob_panic(0, %t20)
  .loc 1 9 7
  trap
}
