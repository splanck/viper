il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 1
  %t1 = trunc1 0
  store i1, %t0, %t1
  br L10
L10:
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 12
  %t2 = trunc1 1
  .loc 1 2 12
  %t3 = zext1 %t2
  .loc 1 2 12
  %t4 = isub.ovf 0, %t3
  .loc 1 2 4
  %t5 = trunc1 %t4
  .loc 1 2 4
  store i1, %t0, %t5
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 10
  %t6 = load i1, %t0
  .loc 1 3 15
  %t7 = trunc1 0
  .loc 1 3 15
  %t8 = zext1 %t7
  .loc 1 3 15
  %t9 = isub.ovf 0, %t8
  .loc 1 3 12
  %t10 = trunc1 %t9
  .loc 1 3 12
  %t11 = zext1 %t6
  .loc 1 3 12
  %t12 = isub.ovf 0, %t11
  .loc 1 3 12
  %t13 = zext1 %t10
  .loc 1 3 12
  %t14 = isub.ovf 0, %t13
  .loc 1 3 12
  %t15 = or %t12, %t14
  .loc 1 3 4
  call @rt_print_i64(%t15)
  .loc 1 3 4
  %t16 = const_str @.L0
  .loc 1 3 4
  call @rt_print_str(%t16)
  .loc 1 3 4
  br exit
exit:
  ret 0
}
