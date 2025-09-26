il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_len(ptr) -> i64
extern @rt_arr_i32_retain(ptr) -> void
extern @rt_arr_i32_release(ptr) -> void
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  store ptr, %t0, null
  br L10
L10:
  .loc 1 1 4
  %t1 = call @rt_arr_i32_new(0)
  .loc 1 1 4
  call @rt_arr_i32_retain(%t1)
  .loc 1 1 4
  %t2 = load ptr, %t0
  .loc 1 1 4
  call @rt_arr_i32_release(%t2)
  .loc 1 1 4
  store ptr, %t0, %t1
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t3 = load ptr, %t0
  .loc 1 2 10
  %t4 = call @rt_arr_i32_len(%t3)
  .loc 1 2 10
  %t5 = isub.ovf %t4, 1
  .loc 1 2 4
  call @rt_print_i64(%t5)
  .loc 1 2 4
  %t6 = const_str @.L0
  .loc 1 2 4
  call @rt_print_str(%t6)
  .loc 1 2 4
  br exit
exit:
  %t7 = load ptr, %t0
  call @rt_arr_i32_release(%t7)
  store ptr, %t0, null
  ret 0
}
