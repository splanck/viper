il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_len(ptr) -> i64
extern @rt_arr_i32_retain(ptr) -> void
extern @rt_arr_i32_release(ptr) -> void
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
  br L10
L10:
  .loc 1 1 4
  %t1 = iadd.ovf 0, 1
  .loc 1 1 4
  %t2 = scmp_lt %t1, 0
  .loc 1 1 4
  cbr %t2, dim_len_fail, dim_len_cont
L20:
  .loc 1 2 10
  %t5 = load ptr, %t0
  .loc 1 2 10
  %t6 = call @rt_arr_i32_len(%t5)
  .loc 1 2 10
  %t7 = isub.ovf %t6, 1
  .loc 1 2 10
  call @rt_print_i64(%t7)
  .loc 1 2 10
  %t8 = const_str @.L0
  .loc 1 2 10
  call @rt_print_str(%t8)
  .loc 1 2 4
  br exit
exit:
  %t9 = load ptr, %t0
  call @rt_arr_i32_release(%t9)
  store ptr, %t0, null
  ret 0
dim_len_fail:
  .loc 1 1 4
  trap
dim_len_cont:
  .loc 1 1 10
  %t3 = call @rt_arr_i32_new(%t1)
  .loc 1 1 10
  call @rt_arr_i32_retain(%t3)
  .loc 1 1 10
  %t4 = load ptr, %t0
  .loc 1 1 10
  call @rt_arr_i32_release(%t4)
  .loc 1 1 10
  store ptr, %t0, %t3
  .loc 1 1 4
  br L20
}
