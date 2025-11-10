il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
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
  br L10
L10:
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  br if_test_0
L30:
  .loc 1 3 16
  %t11 = load i1, %t0
  .loc 1 3 12
  %t12 = alloca 1
  .loc 1 3 12
  cbr %t11, bool_then1, bool_else1
L40:
  .loc 1 4 4
  br if_test_01
exit:
  ret 0
if_test_0:
  .loc 1 2 11
  %t2 = load i1, %t0
  .loc 1 2 7
  %t3 = alloca 1
  .loc 1 2 7
  cbr %t2, bool_then, bool_else
if_then_0:
  .loc 1 2 18
  call @rt_print_i64(1)
  .loc 1 2 18
  %t10 = const_str @.L0
  .loc 1 2 18
  call @rt_print_str(%t10)
  .loc 1 2 4
  br if_exit
if_else:
  .loc 1 2 4
  br if_exit
if_exit:
  .loc 1 2 4
  br L30
bool_then:
  .loc 1 2 7
  %t4 = trunc1 0
  .loc 1 2 7
  store i1, %t3, %t4
  .loc 1 2 7
  br bool_join
bool_else:
  .loc 1 2 7
  %t5 = trunc1 1
  .loc 1 2 7
  store i1, %t3, %t5
  .loc 1 2 7
  br bool_join
bool_join:
  .loc 1 2 7
  %t6 = load i1, %t3
  .loc 1 2 7
  %t7 = zext1 %t6
  .loc 1 2 7
  %t8 = isub.ovf 0, %t7
  .loc 1 2 4
  %t9 = trunc1 %t8
  .loc 1 2 4
  cbr %t9, if_then_0, if_else
bool_then1:
  .loc 1 3 12
  %t13 = trunc1 0
  .loc 1 3 12
  store i1, %t12, %t13
  .loc 1 3 12
  br bool_join1
bool_else1:
  .loc 1 3 12
  %t14 = trunc1 1
  .loc 1 3 12
  store i1, %t12, %t14
  .loc 1 3 12
  br bool_join1
bool_join1:
  .loc 1 3 12
  %t15 = load i1, %t12
  .loc 1 3 12
  %t16 = zext1 %t15
  .loc 1 3 12
  %t17 = isub.ovf 0, %t16
  .loc 1 3 4
  %t18 = trunc1 %t17
  .loc 1 3 4
  store i1, %t0, %t18
  .loc 1 3 4
  br L40
if_test_01:
  .loc 1 4 11
  %t19 = load i1, %t0
  .loc 1 4 7
  %t20 = alloca 1
  .loc 1 4 7
  cbr %t19, bool_then2, bool_else2
if_then_01:
  .loc 1 4 18
  call @rt_print_i64(0)
  .loc 1 4 18
  %t27 = const_str @.L0
  .loc 1 4 18
  call @rt_print_str(%t27)
  .loc 1 4 4
  br if_exit1
if_else1:
  .loc 1 4 4
  br if_exit1
if_exit1:
  .loc 1 4 4
  br exit
bool_then2:
  .loc 1 4 7
  %t21 = trunc1 0
  .loc 1 4 7
  store i1, %t20, %t21
  .loc 1 4 7
  br bool_join2
bool_else2:
  .loc 1 4 7
  %t22 = trunc1 1
  .loc 1 4 7
  store i1, %t20, %t22
  .loc 1 4 7
  br bool_join2
bool_join2:
  .loc 1 4 7
  %t23 = load i1, %t20
  .loc 1 4 7
  %t24 = zext1 %t23
  .loc 1 4 7
  %t25 = isub.ovf 0, %t24
  .loc 1 4 4
  %t26 = trunc1 %t25
  .loc 1 4 4
  cbr %t26, if_then_01, if_else1
}
