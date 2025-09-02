il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
global const str @.L0 = "ONE"
global const str @.L1 = "
"
global const str @.L2 = "TWO"
global const str @.L3 = "OTHER"
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
exit:
  ret 0
if_test_0:
  .loc 1 2 7
  %t1 = load i64, %t0
  .loc 1 2 9
  %t2 = icmp_eq %t1, 1
  .loc 1 2 9
  cbr %t2, label if_then_0, label if_test_1
if_then_0:
  .loc 1 2 24
  %t3 = const_str @.L0
  .loc 1 2 18
  call @rt_print_str(%t3)
  .loc 1 2 18
  %t4 = const_str @.L1
  .loc 1 2 18
  call @rt_print_str(%t4)
  .loc 1 2 4
  br label if_exit
if_test_1:
  .loc 1 2 37
  %t5 = load i64, %t0
  .loc 1 2 39
  %t6 = icmp_eq %t5, 2
  .loc 1 2 39
  cbr %t6, label if_then_1, label if_else
if_then_1:
  .loc 1 2 54
  %t7 = const_str @.L2
  .loc 1 2 48
  call @rt_print_str(%t7)
  .loc 1 2 48
  %t8 = const_str @.L1
  .loc 1 2 48
  call @rt_print_str(%t8)
  .loc 1 2 4
  br label if_exit
if_else:
  .loc 1 2 71
  %t9 = const_str @.L3
  .loc 1 2 65
  call @rt_print_str(%t9)
  .loc 1 2 65
  %t10 = const_str @.L1
  .loc 1 2 65
  call @rt_print_str(%t10)
  .loc 1 2 4
  br label if_exit
if_exit:
  .loc 1 2 4
  br label exit
}
