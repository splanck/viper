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
extern @rt_arr_oob_panic(i64, i64) -> void
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t2 = call @rt_arr_i32_new(2)
  .loc 1 1 4
  store ptr, %t0, %t2
  .loc 1 1 4
  store i64, %t1, 2
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t3 = load ptr, %t0
  .loc 1 2 10
  %t4 = call @rt_arr_i32_len(%t3)
  .loc 1 2 10
  %t5 = scmp_lt 1, 0
  .loc 1 2 10
  %t6 = scmp_ge 1, %t4
  .loc 1 2 10
  %t7 = zext1 %t5
  .loc 1 2 10
  %t8 = zext1 %t6
  .loc 1 2 10
  %t9 = add %t7, %t8
  .loc 1 2 10
  %t10 = scmp_gt %t9, 0
  .loc 1 2 10
  cbr %t10, bc_oob0, bc_ok0
exit:
  ret 0
bc_ok0:
  .loc 1 2 10
  %t11 = call @rt_arr_i32_get(%t3, 1)
  .loc 1 2 4
  call @rt_print_i64(%t11)
  .loc 1 2 4
  %t12 = const_str @.L0
  .loc 1 2 4
  call @rt_print_str(%t12)
  .loc 1 2 4
  br exit
bc_oob0:
  .loc 1 2 10
  call @rt_arr_oob_panic(1, %t4)
  .loc 1 2 10
  trap
}
