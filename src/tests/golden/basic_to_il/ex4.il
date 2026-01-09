il 0.2.0
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
  %t0 = alloca 8
  br L10
L10:
  .loc 1 1 4
  store i64, %t0, 2
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  br if_test_0
L30:
  .loc 1 3 4
  ret 0
exit:
  ret 0
if_test_0:
  .loc 1 2 7
  %t1 = load i64, %t0
  .loc 1 2 9
  %t2 = scmp_le %t1, 1
  .loc 1 2 9
  %t3 = zext1 %t2
  .loc 1 2 9
  %t4 = isub.ovf 0, %t3
  .loc 1 2 4
  %t5 = trunc1 %t4
  .loc 1 2 9
  cbr %t5, if_then_0, if_test_1
if_then_0:
  .loc 1 2 25
  call @rt_print_i64(1)
  .loc 1 2 25
  %t6 = const_str @.L0
  .loc 1 2 25
  call @rt_print_str(%t6)
  .loc 1 2 4
  br if_exit
if_test_1:
  .loc 1 2 34
  %t7 = load i64, %t0
  .loc 1 2 36
  %t8 = scmp_le %t7, 2
  .loc 1 2 36
  %t9 = zext1 %t8
  .loc 1 2 36
  %t10 = isub.ovf 0, %t9
  .loc 1 2 4
  %t11 = trunc1 %t10
  .loc 1 2 36
  cbr %t11, if_then_1, if_else
if_then_1:
  .loc 1 2 52
  call @rt_print_i64(2)
  .loc 1 2 52
  %t12 = const_str @.L0
  .loc 1 2 52
  call @rt_print_str(%t12)
  .loc 1 2 4
  br if_exit
if_else:
  .loc 1 2 65
  call @rt_print_i64(3)
  .loc 1 2 65
  %t13 = const_str @.L0
  .loc 1 2 65
  call @rt_print_str(%t13)
  .loc 1 2 4
  br if_exit
if_exit:
  .loc 1 2 4
  br L30
}
