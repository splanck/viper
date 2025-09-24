il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_trap(str) -> void
extern @rt_arr_i32_new(i64) -> ptr
global const str @.L0 = "bounds check failed: A[i]"
global const str @.L1 = "
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
  %t4 = load i64, %t1
  .loc 1 2 10
  %t5 = scmp_lt 1, 0
  .loc 1 2 10
  %t6 = scmp_ge 1, %t4
  .loc 1 2 10
  %t7 = zext1 %t5
  .loc 1 2 10
  %t8 = zext1 %t6
  .loc 1 2 10
  %t9 = or %t7, %t8
  .loc 1 2 10
  %t10 = trunc1 %t9
  .loc 1 2 10
  cbr %t10, bc_fail0, bc_ok0
exit:
  ret 0
bc_ok0:
  .loc 1 2 10
  %t12 = shl 1, 3
  .loc 1 2 10
  %t13 = gep %t3, %t12
  .loc 1 2 10
  %t14 = load i64, %t13
  .loc 1 2 4
  call @rt_print_i64(%t14)
  .loc 1 2 4
  %t15 = const_str @.L1
  .loc 1 2 4
  call @rt_print_str(%t15)
  .loc 1 2 4
  br exit
bc_fail0:
  .loc 1 2 10
  %t11 = const_str @.L0
  .loc 1 2 10
  call @rt_trap(%t11)
  .loc 1 2 10
  trap
}
