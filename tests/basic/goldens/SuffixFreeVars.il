il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_alloc(i64) -> ptr
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
  %t3 = mul 2, 8
  .loc 1 3 1
  %t4 = call @rt_alloc(%t3)
  .loc 1 3 1
  store ptr, %t0, %t4
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
  %t8 = shl 0, 3
  .loc 1 6 5
  %t9 = gep %t7, %t8
  .loc 1 6 1
  store i64, %t9, %t6
  .loc 1 7 7
  %t10 = load str, %t2
  .loc 1 7 1
  call @rt_print_str(%t10)
  .loc 1 7 1
  %t11 = const_str @.L1
  .loc 1 7 1
  call @rt_print_str(%t11)
  .loc 1 8 7
  %t12 = load i64, %t1
  .loc 1 8 1
  call @rt_print_i64(%t12)
  .loc 1 8 1
  %t13 = const_str @.L1
  .loc 1 8 1
  call @rt_print_str(%t13)
  .loc 1 9 7
  %t14 = load ptr, %t0
  .loc 1 9 7
  %t15 = shl 0, 3
  .loc 1 9 7
  %t16 = gep %t14, %t15
  .loc 1 9 7
  %t17 = load i64, %t16
  .loc 1 9 1
  call @rt_print_i64(%t17)
  .loc 1 9 1
  %t18 = const_str @.L1
  .loc 1 9 1
  call @rt_print_str(%t18)
exit:
  ret 0
}
