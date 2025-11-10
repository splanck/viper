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
  call @rt_str_release_maybe(%t3)
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
  .loc 1 4 12
  %t12 = zext1 %t10
  .loc 1 4 12
  %t13 = isub.ovf 0, %t12
  .loc 1 4 12
  %t14 = zext1 %t11
  .loc 1 4 12
  %t15 = isub.ovf 0, %t14
  .loc 1 4 12
  %t16 = or %t13, %t15
  .loc 1 4 4
  call @rt_print_i64(%t16)
  .loc 1 4 4
  %t17 = const_str @.L0
  .loc 1 4 4
  call @rt_print_str(%t17)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 10
  %t18 = trunc1 0
  .loc 1 5 20
  %t19 = load i1, %t0
  .loc 1 5 16
  %t20 = zext1 %t18
  .loc 1 5 16
  %t21 = isub.ovf 0, %t20
  .loc 1 5 16
  %t22 = zext1 %t19
  .loc 1 5 16
  %t23 = isub.ovf 0, %t22
  .loc 1 5 16
  %t24 = and %t21, %t23
  .loc 1 5 4
  call @rt_print_i64(%t24)
  .loc 1 5 4
  %t25 = const_str @.L0
  .loc 1 5 4
  call @rt_print_str(%t25)
  .loc 1 5 4
  br exit
exit:
  ret 0
}
