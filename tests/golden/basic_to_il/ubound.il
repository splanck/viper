il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_arr_i32_new(i64) -> ptr
extern @rt_arr_i32_len(ptr) -> i64
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t1 = call @rt_arr_i32_new(0)
  .loc 1 1 4
  store ptr, %t0, %t1
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t2 = load ptr, %t0
  .loc 1 2 10
  %t3 = call @rt_arr_i32_len(%t2)
  .loc 1 2 10
  %t4 = sub %t3, 1
  .loc 1 2 4
  call @rt_print_i64(%t4)
  .loc 1 2 4
  %t5 = const_str @.L0
  .loc 1 2 4
  call @rt_print_str(%t5)
  .loc 1 2 4
  br exit
exit:
  ret 0
}
