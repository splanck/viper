il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
global const str @.L0 = "\n"
func @main() -> i64 {
entry:
  %t0 = alloca 1
  %t1 = trunc1 0
  store i1, %t0, %t1
  br L10
L10:
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 12
  %t2 = trunc1 1
  .loc 1 2 4
  store i1, %t0, %t2
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  br if_test_0
L40:
  .loc 1 4 10
  %t5 = load i1, %t0
  .loc 1 4 4
  %t6 = zext1 %t5
  .loc 1 4 4
  %t7 = isub.ovf 0, %t6
  .loc 1 4 4
  call @rt_print_i64(%t7)
  .loc 1 4 4
  %t8 = const_str @.L0
  .loc 1 4 4
  call @rt_print_str(%t8)
  .loc 1 4 4
  br exit
exit:
  ret 0
if_test_0:
  .loc 1 3 7
  %t3 = load i1, %t0
  .loc 1 3 7
  cbr %t3, if_then_0, if_else
if_then_0:
  .loc 1 3 14
  call @rt_print_i64(1)
  .loc 1 3 14
  %t4 = const_str @.L0
  .loc 1 3 14
  call @rt_print_str(%t4)
  .loc 1 3 4
  br if_exit
if_else:
  .loc 1 3 4
  br if_exit
if_exit:
  .loc 1 3 4
  br L40
}
