il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_input_line() -> str
extern @rt_to_int(str) -> i64
extern @rt_alloc(i64) -> ptr
global const str @.L0 = "
"
func @main() -> i64 {
entry:
  %t0 = alloca 1
  store i1, %t0, 0
  %t1 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t2 = call @rt_input_line()
  .loc 1 1 4
  %t3 = call @rt_to_int(%t2)
  .loc 1 1 4
  store i64, %t1, %t3
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 12
  %t4 = load i64, %t1
  .loc 1 3 14
  %t5 = icmp_ne %t4, 0
  .loc 1 3 4
  store i1, %t0, %t5
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 10
  %t6 = load i1, %t0
  .loc 1 4 15
  %t7 = alloca 1
L50:
  .loc 1 5 20
  %t11 = load i1, %t0
  .loc 1 5 20
  %t12 = alloca 1
  .loc 1 5 16
  cbr 0, bool_then1, bool_else1
exit:
  ret 0
bool_then:
bool_else:
  .loc 1 4 12
  store i1, %t7, 1
  .loc 1 4 12
  br bool_join
bool_join:
  .loc 1 4 12
  %t8 = load i1, %t7
  .loc 1 4 4
  %t9 = zext1 %t8
  .loc 1 4 4
  call @rt_print_i64(%t9)
  .loc 1 4 4
  %t10 = const_str @.L0
  .loc 1 4 4
  call @rt_print_str(%t10)
  .loc 1 4 4
  br L50
bool_then1:
  .loc 1 5 16
  store i1, %t12, %t11
  .loc 1 5 16
  br bool_join1
bool_else1:
  .loc 1 5 16
  store i1, %t12, 0
  .loc 1 5 16
  br bool_join1
bool_join1:
  .loc 1 5 16
  %t13 = load i1, %t12
  .loc 1 5 4
  %t14 = zext1 %t13
  .loc 1 5 4
  call @rt_print_i64(%t14)
  .loc 1 5 4
  %t15 = const_str @.L0
  .loc 1 5 4
  call @rt_print_str(%t15)
  .loc 1 5 4
  br exit
}
