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
  store i1, %t0, 1
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  br if_test_0
L40:
  .loc 1 4 10
  %t3 = load i1, %t0
  .loc 1 4 4
  %t4 = zext1 %t3
  .loc 1 4 4
  call @rt_print_i64(%t4)
  .loc 1 4 4
  %t5 = const_str @.L0
  .loc 1 4 4
  call @rt_print_str(%t5)
  .loc 1 4 4
  br exit
exit:
  ret 0
if_test_0:
  .loc 1 3 7
  %t1 = load i1, %t0
  .loc 1 3 7
  cbr %t1, if_then_0, if_else
if_then_0:
  .loc 1 3 14
  call @rt_print_i64(1)
  .loc 1 3 14
  %t2 = const_str @.L0
  .loc 1 3 14
  call @rt_print_str(%t2)
  .loc 1 3 4
  br if_exit
if_else:
  .loc 1 3 4
  br if_exit
if_exit:
  .loc 1 3 4
  br L40
}
