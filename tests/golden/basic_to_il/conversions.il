il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_to_int(str) -> i64
extern @rt_int_to_str(i64) -> str
extern @rt_f64_to_str(f64) -> str
global const str @.L0 = " "
global const str @.L1 = "
"
global const str @.L2 = " 42 "
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  %t2 = alloca 8
  %t3 = alloca 8
  %t4 = alloca 8
  br L10
L10:
  .loc 1 1 4
  store i64, %t4, 1
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  store f64, %t3, 2.5
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 15
  %t5 = load i64, %t4
  .loc 1 3 10
  %t6 = call @rt_int_to_str(%t5)
  .loc 1 3 4
  call @rt_print_str(%t6)
  .loc 1 3 4
  %t7 = const_str @.L0
  .loc 1 3 4
  call @rt_print_str(%t7)
  .loc 1 3 24
  %t8 = load f64, %t3
  .loc 1 3 19
  %t9 = call @rt_f64_to_str(%t8)
  .loc 1 3 4
  call @rt_print_str(%t9)
  .loc 1 3 4
  %t10 = const_str @.L1
  .loc 1 3 4
  call @rt_print_str(%t10)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 13
  %t11 = const_str @.L2
  .loc 1 4 4
  store str, %t2, %t11
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 14
  %t12 = load str, %t2
  .loc 1 5 10
  %t13 = call @rt_to_int(%t12)
  .loc 1 5 4
  call @rt_print_i64(%t13)
  .loc 1 5 4
  %t14 = const_str @.L1
  .loc 1 5 4
  call @rt_print_str(%t14)
  .loc 1 5 4
  br L60
L60:
  .loc 1 6 4
  store f64, %t1, 1.9
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 13
  %t15 = sitofp 0
  .loc 1 7 13
  %t16 = fsub %t15, 1.9
  .loc 1 7 4
  store f64, %t0, %t16
  .loc 1 7 4
  br L80
L80:
  .loc 1 8 14
  %t17 = load f64, %t1
  .loc 1 8 10
  %t18 = fptosi %t17
  .loc 1 8 4
  call @rt_print_i64(%t18)
  .loc 1 8 4
  %t19 = const_str @.L0
  .loc 1 8 4
  call @rt_print_str(%t19)
  .loc 1 8 23
  %t20 = load f64, %t0
  .loc 1 8 19
  %t21 = fptosi %t20
  .loc 1 8 4
  call @rt_print_i64(%t21)
  .loc 1 8 4
  %t22 = const_str @.L1
  .loc 1 8 4
  call @rt_print_str(%t22)
  .loc 1 8 4
  br L90
L90:
  .loc 1 9 4
  br exit
exit:
  ret 0
}
