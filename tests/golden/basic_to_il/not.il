il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_alloc(i64) -> ptr
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 1
  store i1, %t0, 0
  br L10
L10:
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  br if_test_0
L30:
  .loc 1 3 16
  %t6 = load i1, %t0
  .loc 1 3 12
  %t7 = zext1 %t6
  .loc 1 3 12
  %t8 = xor %t7, 1
  .loc 1 3 12
  %t9 = trunc1 %t8
  .loc 1 3 4
  store i1, %t0, %t9
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  br if_test_01
exit:
  ret 0
if_test_0:
  .loc 1 2 11
  %t1 = load i1, %t0
  .loc 1 2 7
  %t2 = zext1 %t1
  .loc 1 2 7
  %t3 = xor %t2, 1
  .loc 1 2 7
  %t4 = trunc1 %t3
  .loc 1 2 7
  cbr %t4, if_then_0, if_else
if_then_0:
  .loc 1 2 18
  call @rt_print_i64(1)
  .loc 1 2 18
  %t5 = const_str @.L0
  .loc 1 2 18
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
  .loc 1 4 11
  %t10 = load i1, %t0
  .loc 1 4 7
  %t11 = zext1 %t10
  .loc 1 4 7
  %t12 = xor %t11, 1
  .loc 1 4 7
  %t13 = trunc1 %t12
  .loc 1 4 7
  cbr %t13, if_then_01, if_else1
if_then_01:
  .loc 1 4 18
  call @rt_print_i64(0)
  .loc 1 4 18
  %t14 = const_str @.L0
  .loc 1 4 18
  call @rt_print_str(%t14)
  .loc 1 4 4
  br if_exit1
if_else1:
  .loc 1 4 4
  br if_exit1
if_exit1:
  .loc 1 4 4
  br exit
}
