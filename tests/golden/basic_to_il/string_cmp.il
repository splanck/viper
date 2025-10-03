il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_eq(str, str) -> i1
global const str @.L0 = "HELLO"
global const str @.L1 = "
"
global const str @.L2 = "X"
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
  .loc 1 2 4
  br if_test_0
L30:
  .loc 1 3 4
  br if_test_01
L40:
  .loc 1 4 4
  br exit
exit:
  ret 0
if_test_0:
  .loc 1 2 7
  %t2 = load str, %t0
  .loc 1 2 12
  %t3 = const_str @.L0
  .loc 1 2 10
  %t4 = call @rt_str_eq(%t2, %t3)
  .loc 1 2 10
  %t5 = zext1 %t4
  .loc 1 2 10
  %t6 = isub.ovf 0, %t5
  .loc 1 2 4
  %t7 = trunc1 %t6
  .loc 1 2 4
  cbr %t7, if_then_0, if_else
if_then_0:
  .loc 1 2 25
  call @rt_print_i64(1)
  .loc 1 2 25
  %t8 = const_str @.L1
  .loc 1 2 25
  call @rt_print_str(%t8)
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
  %t9 = load str, %t0
  .loc 1 3 13
  %t10 = const_str @.L2
  .loc 1 3 10
  %t11 = call @rt_str_eq(%t9, %t10)
  .loc 1 3 10
  %t12 = zext1 %t11
  .loc 1 3 10
  %t13 = isub.ovf 0, %t12
  .loc 1 3 10
  %t14 = xor %t13, -1
  .loc 1 3 4
  %t15 = trunc1 %t14
  .loc 1 3 4
  cbr %t15, if_then_01, if_else1
if_then_01:
  .loc 1 3 22
  call @rt_print_i64(2)
  .loc 1 3 22
  %t16 = const_str @.L1
  .loc 1 3 22
  call @rt_print_str(%t16)
  .loc 1 3 4
  br if_exit1
if_else1:
  .loc 1 3 4
  br if_exit1
if_exit1:
  .loc 1 3 4
  br L40
}
