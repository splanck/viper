il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_trap(str) -> void
extern @rt_str_empty() -> str
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_len(ptr) -> i64
extern @rt_arr_i32_get(ptr, i64) -> i64
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
  store ptr, %t0, null
  %t1 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t2 = iadd.ovf 2, 1
  .loc 1 1 4
  %t3 = scmp_lt %t2, 0
  .loc 1 1 4
  cbr %t3, dim_len_fail, dim_len_cont
L20:
  .loc 1 2 10
  %t6 = load ptr, %t0
  .loc 1 2 10
  %t7 = call @rt_arr_i32_len(%t6)
  .loc 1 2 10
  %t8 = scmp_lt 1, 0
  .loc 1 2 10
  %t9 = scmp_ge 1, %t7
  .loc 1 2 10
  %t10 = zext1 %t8
  .loc 1 2 10
  %t11 = zext1 %t9
  .loc 1 2 10
  %t12 = or %t10, %t11
  .loc 1 2 10
  %t13 = icmp_ne %t12, 0
  .loc 1 2 10
  cbr %t13, bc_oob0, bc_ok0
exit:
  %t16 = load ptr, %t0
  call @rt_arr_i32_release(%t16)
  store ptr, %t0, null
  ret 0
dim_len_fail:
  .loc 1 1 4
  trap
dim_len_cont:
  .loc 1 1 4
  %t4 = call @rt_arr_i32_new(%t2)
  .loc 1 1 4
  call @rt_arr_i32_retain(%t4)
  .loc 1 1 4
  %t5 = load ptr, %t0
  .loc 1 1 4
  call @rt_arr_i32_release(%t5)
  .loc 1 1 4
  store ptr, %t0, %t4
  .loc 1 1 4
  store i64, %t1, %t2
  .loc 1 1 4
  br L20
bc_ok0:
  .loc 1 2 10
  %t14 = call @rt_arr_i32_get(%t6, 1)
  .loc 1 2 4
  call @rt_print_i64(%t14)
  .loc 1 2 4
  %t15 = const_str @.L0
  .loc 1 2 4
  call @rt_print_str(%t15)
  .loc 1 2 4
  br exit
bc_oob0:
  .loc 1 2 10
  call @rt_arr_oob_panic(1, %t7)
  .loc 1 2 10
  trap
}
