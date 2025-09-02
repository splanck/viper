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
  store i64, %t0, 2
  .loc 1 1 4
  br label L20
L20:
  .loc 1 2 4
  br label if_test_0
L30:
  .loc 1 3 4
  br label exit
exit:
  ret 0
if_test_0:
  .loc 1 2 7
  %t1 = load i64, %t0
  .loc 1 2 9
  %t2 = scmp_le %t1, 1
  .loc 1 2 9
  cbr %t2, label if_then_0, label if_test_1
if_then_0:
  .loc 1 2 19
  call @rt_print_i64(1)
  .loc 1 2 19
  %t3 = const_str @.L0
  .loc 1 2 19
  call @rt_print_str(%t3)
  .loc 1 2 4
  br label if_exit
if_test_1:
  .loc 1 2 34
  %t4 = load i64, %t0
  .loc 1 2 36
  %t5 = scmp_le %t4, 2
  .loc 1 2 36
  cbr %t5, label if_then_1, label if_else
if_then_1:
  .loc 1 2 46
  call @rt_print_i64(2)
  .loc 1 2 46
  %t6 = const_str @.L0
  .loc 1 2 46
  call @rt_print_str(%t6)
  .loc 1 2 4
  br label if_exit
if_else:
  .loc 1 2 59
  call @rt_print_i64(3)
  .loc 1 2 59
  %t7 = const_str @.L0
  .loc 1 2 59
  call @rt_print_str(%t7)
  .loc 1 2 4
  br label if_exit
if_exit:
  .loc 1 2 4
  br label L30
}
