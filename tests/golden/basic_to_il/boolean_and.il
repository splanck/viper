il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_input_line() -> str
extern @rt_to_int(str) -> i64
extern @rt_str_empty() -> str
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 1
  %t1 = trunc1 0
  store i1, %t0, %t1
  %t2 = alloca 1
  %t3 = trunc1 0
  store i1, %t2, %t3
  %t4 = alloca 8
  %t5 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t6 = call @rt_input_line()
  .loc 1 1 4
  %t7 = call @rt_to_int(%t6)
  .loc 1 1 4
  store i64, %t5, %t7
  .loc 1 1 4
  call @rt_str_release_maybe(%t6)
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  %t8 = call @rt_input_line()
  .loc 1 2 4
  %t9 = call @rt_to_int(%t8)
  .loc 1 2 4
  store i64, %t4, %t9
  .loc 1 2 4
  call @rt_str_release_maybe(%t8)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 14
  %t10 = load i64, %t5
  .loc 1 5 16
  %t11 = icmp_ne %t10, 0
  .loc 1 5 16
  %t12 = zext1 %t11
  .loc 1 5 16
  %t13 = isub.ovf 0, %t12
  .loc 1 5 4
  %t14 = trunc1 %t13
  .loc 1 5 4
  store i1, %t2, %t14
  .loc 1 5 4
  br L60
L60:
  .loc 1 6 17
  %t15 = load i1, %t2
  .loc 1 6 26
  %t16 = load i64, %t4
  .loc 1 6 28
  %t17 = icmp_ne %t16, 0
  .loc 1 6 28
  %t18 = zext1 %t17
  .loc 1 6 28
  %t19 = isub.ovf 0, %t18
  .loc 1 6 21
  %t20 = trunc1 %t19
  .loc 1 6 21
  %t21 = zext1 %t15
  .loc 1 6 21
  %t22 = isub.ovf 0, %t21
  .loc 1 6 21
  %t23 = zext1 %t20
  .loc 1 6 21
  %t24 = isub.ovf 0, %t23
  .loc 1 6 21
  %t25 = and %t22, %t24
  .loc 1 6 4
  %t26 = trunc1 %t25
  .loc 1 6 4
  store i1, %t0, %t26
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 10
  %t27 = load i1, %t0
  .loc 1 7 4
  %t28 = zext1 %t27
  .loc 1 7 4
  %t29 = isub.ovf 0, %t28
  .loc 1 7 4
  call @rt_print_i64(%t29)
  .loc 1 7 4
  %t30 = const_str @.L0
  .loc 1 7 4
  call @rt_print_str(%t30)
  .loc 1 7 4
  br exit
exit:
  ret 0
}
