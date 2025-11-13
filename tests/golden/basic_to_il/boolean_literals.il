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
  .loc 1 4 12
  %t11 = zext1 %t10
  .loc 1 4 12
  %t12 = isub.ovf 0, %t11
  .loc 1 4 12
  %t13 = or %t12, -1
  .loc 1 4 12
  call @rt_print_i64(%t13)
  .loc 1 4 12
  %t14 = const_str @.L0
  .loc 1 4 12
  call @rt_print_str(%t14)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 20
  %t15 = load i1, %t0
  .loc 1 5 16
  %t16 = zext1 %t15
  .loc 1 5 16
  %t17 = isub.ovf 0, %t16
  .loc 1 5 16
  %t18 = and 0, %t17
  .loc 1 5 16
  call @rt_print_i64(%t18)
  .loc 1 5 16
  %t19 = const_str @.L0
  .loc 1 5 16
  call @rt_print_str(%t19)
  .loc 1 5 4
  br exit
exit:
  ret 0
}
