il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_eq(str, str) -> i1
extern @rt_str_empty() -> str
extern @rt_str_retain_maybe(str) -> void
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "HELLO"
global const str @.L1 = "\n"
global const str @.L2 = "X"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  %t1 = call @rt_str_empty()
  store str, %t0, %t1
  br L10
L10:
  .loc 1 1 13
  %t2 = const_str @.L0
  .loc 1 1 4
  %t3 = load str, %t0
  .loc 1 1 4
  call @rt_str_release_maybe(%t3)
  .loc 1 1 4
  call @rt_str_retain_maybe(%t2)
  .loc 1 1 4
  store str, %t0, %t2
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  br if_test_0
L30:
  .loc 1 3 4
  br if_test_01
L40:
  .loc 1 4 4
  ret 0
exit:
  ret 0
if_test_0:
  .loc 1 2 7
  %t4 = load str, %t0
  .loc 1 2 12
  %t5 = const_str @.L0
  .loc 1 2 10
  %t6 = call @rt_str_eq(%t4, %t5)
  .loc 1 2 10
  %t7 = zext1 %t6
  .loc 1 2 10
  %t8 = isub.ovf 0, %t7
  .loc 1 2 4
  %t9 = trunc1 %t8
  .loc 1 2 10
  cbr %t9, if_then_0, if_else
if_then_0:
  .loc 1 2 31
  call @rt_print_i64(1)
  .loc 1 2 31
  %t10 = const_str @.L1
  .loc 1 2 31
  call @rt_print_str(%t10)
  .loc 1 2 4
  br if_exit
if_else:
  .loc 1 2 4
  br if_exit
if_exit:
  .loc 1 2 4
  br L30
if_test_01:
  .loc 1 3 7
  %t11 = load str, %t0
  .loc 1 3 13
  %t12 = const_str @.L2
  .loc 1 3 10
  %t13 = call @rt_str_eq(%t11, %t12)
  .loc 1 3 10
  %t14 = zext1 %t13
  .loc 1 3 10
  %t15 = isub.ovf 0, %t14
  .loc 1 3 10
  %t16 = xor %t15, -1
  .loc 1 3 4
  %t17 = trunc1 %t16
  .loc 1 3 10
  cbr %t17, if_then_01, if_else1
if_then_01:
  .loc 1 3 28
  call @rt_print_i64(2)
  .loc 1 3 28
  %t18 = const_str @.L1
  .loc 1 3 28
  call @rt_print_str(%t18)
  .loc 1 3 4
  br if_exit1
if_else1:
  .loc 1 3 4
  br if_exit1
if_exit1:
  .loc 1 3 4
  br L40
}
