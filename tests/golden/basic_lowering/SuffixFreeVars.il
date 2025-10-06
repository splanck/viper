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
extern @rt_str_retain_maybe(str) -> void
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "ok"
global const str @.L1 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  store ptr, %t0, null
  %t1 = alloca 8
  %t2 = alloca 8
  br L-1000000000
L-1000000000:
  .loc 1 1 1
  br L-999999999
L-999999999:
  .loc 1 2 1
  br L-999999998
L-999999998:
  .loc 1 3 1
  %t3 = iadd.ovf 2, 1
  .loc 1 3 1
  %t4 = scmp_lt %t3, 0
  .loc 1 3 1
  cbr %t4, dim_len_fail, dim_len_cont
L-999999997:
  .loc 1 4 15
  %t7 = const_str @.L0
  .loc 1 4 1
  %t8 = load str, %t2
  .loc 1 4 1
  call @rt_str_release_maybe(%t8)
  .loc 1 4 1
  call @rt_str_retain_maybe(%t7)
  .loc 1 4 1
  store str, %t2, %t7
  .loc 1 4 1
  br L-999999996
L-999999996:
  .loc 1 5 1
  store i64, %t1, 5
  .loc 1 5 1
  br L-999999995
L-999999995:
  .loc 1 6 17
  %t9 = load i64, %t1
  .loc 1 6 17
  %t10 = load ptr, %t0
  .loc 1 6 5
  %t11 = call @rt_arr_i32_len(%t10)
  .loc 1 6 5
  %t12 = scmp_lt 0, 0
  .loc 1 6 5
  %t13 = scmp_ge 0, %t11
  .loc 1 6 5
  %t14 = zext1 %t12
  .loc 1 6 5
  %t15 = zext1 %t13
  .loc 1 6 5
  %t16 = or %t14, %t15
  .loc 1 6 5
  %t17 = icmp_ne %t16, 0
  .loc 1 6 5
  cbr %t17, bc_oob0, bc_ok0
L-999999994:
  .loc 1 7 7
  %t18 = load str, %t2
  .loc 1 7 1
  call @rt_print_str(%t18)
  .loc 1 7 1
  %t19 = const_str @.L1
  .loc 1 7 1
  call @rt_print_str(%t19)
  .loc 1 7 1
  br L-999999993
L-999999993:
  .loc 1 8 7
  %t20 = load i64, %t1
  .loc 1 8 1
  call @rt_print_i64(%t20)
  .loc 1 8 1
  %t21 = const_str @.L1
  .loc 1 8 1
  call @rt_print_str(%t21)
  .loc 1 8 1
  br L-999999992
L-999999992:
  .loc 1 9 7
  %t22 = load ptr, %t0
  .loc 1 9 7
  %t23 = call @rt_arr_i32_len(%t22)
  .loc 1 9 7
  %t24 = scmp_lt 0, 0
  .loc 1 9 7
  %t25 = scmp_ge 0, %t23
  .loc 1 9 7
  %t26 = zext1 %t24
  .loc 1 9 7
  %t27 = zext1 %t25
  .loc 1 9 7
  %t28 = or %t26, %t27
  .loc 1 9 7
  %t29 = icmp_ne %t28, 0
  .loc 1 9 7
  cbr %t29, bc_oob1, bc_ok1
exit:
  %t32 = load ptr, %t0
  call @rt_arr_i32_release(%t32)
  store ptr, %t0, null
  ret 0
dim_len_fail:
  .loc 1 3 1
  trap
dim_len_cont:
  .loc 1 3 1
  %t5 = call @rt_arr_i32_new(%t3)
  .loc 1 3 1
  call @rt_arr_i32_retain(%t5)
  .loc 1 3 1
  %t6 = load ptr, %t0
  .loc 1 3 1
  call @rt_arr_i32_release(%t6)
  .loc 1 3 1
  store ptr, %t0, %t5
  .loc 1 3 1
  br L-999999997
bc_ok0:
  .loc 1 6 1
  call @rt_arr_i32_set(%t10, 0, %t9)
  .loc 1 6 1
  br L-999999994
bc_oob0:
  .loc 1 6 5
  call @rt_arr_oob_panic(0, %t11)
  .loc 1 6 5
  trap
bc_ok1:
  .loc 1 9 7
  %t30 = call @rt_arr_i32_get(%t22, 0)
  .loc 1 9 1
  call @rt_print_i64(%t30)
  .loc 1 9 1
  %t31 = const_str @.L1
  .loc 1 9 1
  call @rt_print_str(%t31)
  .loc 1 9 1
  br exit
bc_oob1:
  .loc 1 9 7
  call @rt_arr_oob_panic(0, %t23)
  .loc 1 9 7
  trap
}
