il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 8
  br label L10
L10:
  .loc 1 1 4
  store i64, %t0, 0
  .loc 1 1 4
  br label L20
L20:
  .loc 1 2 4
  br label if_test_0
L30:
  .loc 1 3 4
  br label if_test_01
exit:
  ret 0
if_test_0:
  .loc 1 2 11
  %t1 = load i64, %t0
  .loc 1 2 7
  %t2 = trunc1 %t1
  .loc 1 2 7
  %t3 = zext1 %t2
  .loc 1 2 7
  %t4 = xor %t3, 1
  .loc 1 2 7
  %t5 = trunc1 %t4
  .loc 1 2 7
  cbr %t5, label if_then_0, label if_else
if_then_0:
  .loc 1 2 18
  call @rt_print_i64(1)
  .loc 1 2 18
  %t6 = const_str @.L0
  .loc 1 2 18
  call @rt_print_str(%t6)
  .loc 1 2 4
  br label if_exit
if_else:
  .loc 1 2 4
  br label if_exit
if_exit:
  .loc 1 2 4
  br label L30
if_test_01:
  .loc 1 3 4
  %t7 = trunc1 0
  .loc 1 3 4
  cbr %t7, label if_then_01, label if_else1
if_then_01:
  .loc 1 3 18
  call @rt_print_i64(0)
  .loc 1 3 18
  %t8 = const_str @.L0
  .loc 1 3 18
  call @rt_print_str(%t8)
  .loc 1 3 4
  br label if_exit1
if_else1:
  .loc 1 3 4
  br label if_exit1
if_exit1:
  .loc 1 3 4
  br label exit
}
