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
  cbr %t4, if_then_0, if_else
if_then_0:
  .loc 1 2 25
  call @rt_print_i64(1)
  .loc 1 2 25
  %t5 = const_str @.L1
  .loc 1 2 25
  call @rt_print_str(%t5)
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
  %t6 = load str, %t0
  .loc 1 3 13
  %t7 = const_str @.L2
  .loc 1 3 10
  %t8 = call @rt_str_eq(%t6, %t7)
  .loc 1 3 10
  %t9 = zext1 %t8
  .loc 1 3 10
  %t10 = xor %t9, 1
  .loc 1 3 10
  %t11 = trunc1 %t10
  .loc 1 3 10
  cbr %t11, if_then_01, if_else1
if_then_01:
  .loc 1 3 22
  call @rt_print_i64(2)
  .loc 1 3 22
  %t12 = const_str @.L1
  .loc 1 3 22
  call @rt_print_str(%t12)
  .loc 1 3 4
  br if_exit1
if_else1:
  .loc 1 3 4
  br if_exit1
if_exit1:
  .loc 1 3 4
  br L40
}
