il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_trap(str) -> void
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_len(ptr) -> i64
extern @rt_arr_i32_get(ptr, i64) -> i64
extern @rt_arr_i32_retain(ptr) -> void
extern @rt_arr_i32_release(ptr) -> void
extern @rt_arr_oob_panic(i64, i64) -> void
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  store ptr, %t0, null
  %t1 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t2 = call @rt_arr_i32_new(2)
  .loc 1 1 4
  call @rt_arr_i32_retain(%t2)
  .loc 1 1 4
  %t3 = load ptr, %t0
  .loc 1 1 4
  call @rt_arr_i32_release(%t3)
  .loc 1 1 4
  store ptr, %t0, %t2
  .loc 1 1 4
  store i64, %t1, 2
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t4 = load ptr, %t0
  .loc 1 2 10
  %t5 = call @rt_arr_i32_len(%t4)
  .loc 1 2 10
  %t6 = scmp_lt 1, 0
  .loc 1 2 10
  %t7 = scmp_ge 1, %t5
  .loc 1 2 10
  %t8 = zext1 %t6
  .loc 1 2 10
  %t9 = zext1 %t7
  .loc 1 2 10
  %t10 = or %t8, %t9
  .loc 1 2 10
  %t11 = icmp_ne %t10, 0
  .loc 1 2 10
  cbr %t11, bc_oob0, bc_ok0
exit:
  %t14 = load ptr, %t0
  call @rt_arr_i32_release(%t14)
  store ptr, %t0, null
  ret 0
bc_ok0:
  .loc 1 2 10
  %t12 = call @rt_arr_i32_get(%t4, 1)
  .loc 1 2 4
  call @rt_print_i64(%t12)
  .loc 1 2 4
  %t13 = const_str @.L0
  .loc 1 2 4
  call @rt_print_str(%t13)
  .loc 1 2 4
  br exit
bc_oob0:
  .loc 1 2 10
  call @rt_arr_oob_panic(1, %t5)
  .loc 1 2 10
  trap
}
