il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_arr_i32_new(i64) -> ptr
global const str @.L0 = "ok"
global const str @.L1 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
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
  store ptr, %t0, %t3
  .loc 1 4 15
  %t4 = const_str @.L0
  .loc 1 4 1
  store str, %t2, %t4
  .loc 1 5 1
  store i64, %t1, 5
  .loc 1 6 17
  %t5 = load i64, %t1
  .loc 1 6 17
  %t6 = load ptr, %t0
  .loc 1 6 5
  %t7 = shl 0, 3
  .loc 1 6 5
  %t8 = gep %t6, %t7
  .loc 1 6 1
  store i64, %t8, %t5
  .loc 1 7 7
  %t9 = load str, %t2
  .loc 1 7 1
  call @rt_print_str(%t9)
  .loc 1 7 1
  %t10 = const_str @.L1
  .loc 1 7 1
  call @rt_print_str(%t10)
  .loc 1 8 7
  %t11 = load i64, %t1
  .loc 1 8 1
  call @rt_print_i64(%t11)
  .loc 1 8 1
  %t12 = const_str @.L1
  .loc 1 8 1
  call @rt_print_str(%t12)
  .loc 1 9 7
  %t13 = load ptr, %t0
  .loc 1 9 7
  %t14 = shl 0, 3
  .loc 1 9 7
  %t15 = gep %t13, %t14
  .loc 1 9 7
  %t16 = load i64, %t15
  .loc 1 9 1
  call @rt_print_i64(%t16)
  .loc 1 9 1
  %t17 = const_str @.L1
  .loc 1 9 1
  call @rt_print_str(%t17)
exit:
  ret 0
}
