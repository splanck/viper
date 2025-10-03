il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_input_line() -> str
extern @rt_to_int(str) -> i64
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 1
  %t1 = trunc1 0
  store i1, %t0, %t1
  %t2 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t3 = call @rt_input_line()
  .loc 1 1 4
  %t4 = call @rt_to_int(%t3)
  .loc 1 1 4
  store i64, %t2, %t4
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 12
  %t5 = load i64, %t2
  .loc 1 3 14
  %t6 = icmp_ne %t5, 0
  .loc 1 3 14
  %t7 = zext1 %t6
  .loc 1 3 14
  %t8 = isub.ovf 0, %t7
  .loc 1 3 4
  %t9 = trunc1 %t8
  .loc 1 3 4
  store i1, %t0, %t9
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 10
  %t10 = load i1, %t0
  .loc 1 4 15
  %t11 = trunc1 1
  .loc 1 4 15
  %t12 = zext1 %t11
  .loc 1 4 15
  %t13 = isub.ovf 0, %t12
  .loc 1 4 12
  %t14 = trunc1 %t13
  .loc 1 4 12
  %t15 = zext1 %t10
  .loc 1 4 12
  %t16 = isub.ovf 0, %t15
  .loc 1 4 12
  %t17 = zext1 %t14
  .loc 1 4 12
  %t18 = isub.ovf 0, %t17
  .loc 1 4 12
  %t19 = or %t16, %t18
  .loc 1 4 4
  call @rt_print_i64(%t19)
  .loc 1 4 4
  %t20 = const_str @.L0
  .loc 1 4 4
  call @rt_print_str(%t20)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 10
  %t21 = trunc1 0
  .loc 1 5 10
  %t22 = zext1 %t21
  .loc 1 5 10
  %t23 = isub.ovf 0, %t22
  .loc 1 5 16
  %t24 = trunc1 %t23
  .loc 1 5 20
  %t25 = load i1, %t0
  .loc 1 5 16
  %t26 = zext1 %t24
  .loc 1 5 16
  %t27 = isub.ovf 0, %t26
  .loc 1 5 16
  %t28 = zext1 %t25
  .loc 1 5 16
  %t29 = isub.ovf 0, %t28
  .loc 1 5 16
  %t30 = and %t27, %t29
  .loc 1 5 4
  call @rt_print_i64(%t30)
  .loc 1 5 4
  %t31 = const_str @.L0
  .loc 1 5 4
  call @rt_print_str(%t31)
  .loc 1 5 4
  br exit
exit:
  ret 0
}
