il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_trap(str) -> void
extern @rt_alloc(i64) -> ptr
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
  %t2 = mul 2, 8
  .loc 1 1 4
  %t3 = call @rt_alloc(%t2)
  .loc 1 1 4
  store ptr, %t0, %t3
  .loc 1 1 4
  store i64, %t1, 2
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t4 = load ptr, %t0
  .loc 1 2 10
  %t5 = load i64, %t1
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
  %t11 = trunc1 %t10
  .loc 1 2 10
  cbr %t11, bc_fail0, bc_ok0
exit:
  ret 0
bc_ok0:
  .loc 1 2 10
  %t13 = shl 1, 3
  .loc 1 2 10
  %t14 = gep %t4, %t13
  .loc 1 2 10
  %t15 = load i64, %t14
  .loc 1 2 4
  call @rt_print_i64(%t15)
  .loc 1 2 4
  %t16 = const_str @.L1
  .loc 1 2 4
  call @rt_print_str(%t16)
  .loc 1 2 4
  br exit
bc_fail0:
  .loc 1 2 10
  %t12 = const_str @.L0
  .loc 1 2 10
  call @rt_trap(%t12)
  .loc 1 2 10
  trap
}
