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
  %t5 = load i1, %t0
  .loc 1 3 12
  %t6 = alloca 1
  .loc 1 3 12
  cbr %t5, bool_then1, bool_else1
L40:
  .loc 1 4 4
  br if_test_01
exit:
  ret 0
if_test_0:
  .loc 1 2 11
  %t1 = load i1, %t0
  .loc 1 2 7
  %t2 = alloca 1
  .loc 1 2 7
  cbr %t1, bool_then, bool_else
if_then_0:
  .loc 1 2 18
  call @rt_print_i64(1)
  .loc 1 2 18
  %t4 = const_str @.L0
  .loc 1 2 18
  call @rt_print_str(%t4)
  .loc 1 2 4
  br if_exit
if_else:
  .loc 1 2 4
  br if_exit
if_exit:
  .loc 1 2 4
  br L30
bool_then:
  .loc 1 2 7
  store i1, %t2, 0
  .loc 1 2 7
  br bool_join
bool_else:
  .loc 1 2 7
  store i1, %t2, 1
  .loc 1 2 7
  br bool_join
bool_join:
  .loc 1 2 7
  %t3 = load i1, %t2
  .loc 1 2 7
  cbr %t3, if_then_0, if_else
bool_then1:
  .loc 1 3 12
  store i1, %t6, 0
  .loc 1 3 12
  br bool_join1
bool_else1:
  .loc 1 3 12
  store i1, %t6, 1
  .loc 1 3 12
  br bool_join1
bool_join1:
  .loc 1 3 12
  %t7 = load i1, %t6
  .loc 1 3 4
  store i1, %t0, %t7
  .loc 1 3 4
  br L40
if_test_01:
  .loc 1 4 11
  %t8 = load i1, %t0
  .loc 1 4 7
  %t9 = alloca 1
  .loc 1 4 7
  cbr %t8, bool_then2, bool_else2
if_then_01:
  .loc 1 4 18
  call @rt_print_i64(0)
  .loc 1 4 18
  %t11 = const_str @.L0
  .loc 1 4 18
  call @rt_print_str(%t11)
  .loc 1 4 4
  br if_exit1
if_else1:
  .loc 1 4 4
  br if_exit1
if_exit1:
  .loc 1 4 4
  br exit
bool_then2:
  .loc 1 4 7
  store i1, %t9, 0
  .loc 1 4 7
  br bool_join2
bool_else2:
  .loc 1 4 7
  store i1, %t9, 1
  .loc 1 4 7
  br bool_join2
bool_join2:
  .loc 1 4 7
  %t10 = load i1, %t9
  .loc 1 4 7
  cbr %t10, if_then_01, if_else1
}
