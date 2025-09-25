il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_resize(ptr, i64) -> ptr
extern @rt_arr_i32_retain(ptr) -> void
extern @rt_arr_i32_release(ptr) -> void
func @main() -> i64 {
entry:
  %t0 = alloca 8
  store ptr, %t0, null
  br L10
L10:
  .loc 1 1 4
  %t1 = call @rt_arr_i32_new(3)
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
  .loc 1 2 4
  %t3 = load ptr, %t0
  .loc 1 2 4
  %t4 = call @rt_arr_i32_resize(%t3, 5)
  .loc 1 2 4
  call @rt_arr_i32_retain(%t4)
  .loc 1 2 4
  %t5 = load ptr, %t0
  .loc 1 2 4
  call @rt_arr_i32_release(%t5)
  .loc 1 2 4
  store ptr, %t0, %t4
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  br exit
exit:
  %t6 = load ptr, %t0
  call @rt_arr_i32_release(%t6)
  store ptr, %t0, null
  ret 0
}
