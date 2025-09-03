il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
global const str @.L0 = "HELLO"
global const str @.L1 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  br L10
L10:
  .loc 1 1 13
  %t1 = const_str @.L0
  .loc 1 1 4
  store str, %t0, %t1
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 14
  %t2 = load str, %t0
  .loc 1 2 10
  %t3 = call @rt_len(%t2)
  .loc 1 2 4
  call @rt_print_i64(%t3)
  .loc 1 2 4
  %t4 = const_str @.L1
  .loc 1 2 4
  call @rt_print_str(%t4)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 15
  %t5 = load str, %t0
  .loc 1 3 19
  %t6 = add 2, -1
  .loc 1 3 10
  %t7 = call @rt_substr(%t5, %t6, 3)
  .loc 1 3 4
  call @rt_print_str(%t7)
  .loc 1 3 4
  %t8 = const_str @.L1
  .loc 1 3 4
  call @rt_print_str(%t8)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 15
  %t9 = load str, %t0
  .loc 1 4 19
  %t10 = add 3, -1
  .loc 1 4 10
  %t11 = call @rt_substr(%t9, %t10, 9223372036854775807)
  .loc 1 4 4
  call @rt_print_str(%t11)
  .loc 1 4 4
  %t12 = const_str @.L1
  .loc 1 4 4
  call @rt_print_str(%t12)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 16
  %t13 = load str, %t0
  .loc 1 5 10
  %t14 = call @rt_substr(%t13, 0, 2)
  .loc 1 5 4
  call @rt_print_str(%t14)
  .loc 1 5 4
  %t15 = const_str @.L1
  .loc 1 5 4
  call @rt_print_str(%t15)
  .loc 1 5 4
  br L60
L60:
  .loc 1 6 17
  %t16 = load str, %t0
  .loc 1 6 10
  %t17 = call @rt_len(%t16)
  .loc 1 6 10
  %t18 = mul 2, -1
  .loc 1 6 10
  %t19 = add %t17, %t18
  .loc 1 6 10
  %t20 = call @rt_substr(%t16, %t19, 2)
  .loc 1 6 4
  call @rt_print_str(%t20)
  .loc 1 6 4
  %t21 = const_str @.L1
  .loc 1 6 4
  call @rt_print_str(%t21)
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 4
  br exit
exit:
  ret 0
}
