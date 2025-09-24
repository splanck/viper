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
  br L20
L20:
  .loc 1 2 4
  %t8 = call @rt_input_line()
  .loc 1 2 4
  %t9 = call @rt_to_int(%t8)
  .loc 1 2 4
  store i64, %t4, %t9
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
  .loc 1 5 4
  store i1, %t2, %t11
  .loc 1 5 4
  br L60
L60:
  .loc 1 6 17
  %t12 = load i1, %t2
  .loc 1 6 21
  %t13 = alloca 1
  .loc 1 6 21
  cbr %t12, or_true, or_rhs
L70:
  .loc 1 7 10
  %t18 = load i1, %t0
  .loc 1 7 4
  %t19 = zext1 %t18
  .loc 1 7 4
  call @rt_print_i64(%t19)
  .loc 1 7 4
  %t20 = const_str @.L0
  .loc 1 7 4
  call @rt_print_str(%t20)
  .loc 1 7 4
  br exit
exit:
  ret 0
or_true:
  .loc 1 6 21
  %t14 = trunc1 1
  .loc 1 6 21
  store i1, %t13, %t14
  .loc 1 6 21
  br or_done
or_rhs:
  .loc 1 6 29
  %t15 = load i64, %t4
  .loc 1 6 31
  %t16 = icmp_ne %t15, 0
  .loc 1 6 21
  store i1, %t13, %t16
  .loc 1 6 21
  br or_done
or_done:
  .loc 1 6 21
  %t17 = load i1, %t13
  .loc 1 6 4
  store i1, %t0, %t17
  .loc 1 6 4
  br L70
}
