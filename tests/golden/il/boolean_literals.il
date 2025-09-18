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
  %t1 = trunc1 0
  store i1, %t0, %t1
  %t2 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t3 = call @rt_input_line()
  .loc 1 1 4
  %t4 = call @rt_to_int(%t3)
  .loc 1 1 4
  store i64, %t2, %t4
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 12
  %t5 = load i64, %t2
  .loc 1 3 14
  %t6 = icmp_ne %t5, 0
  .loc 1 3 4
  store i1, %t0, %t6
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 10
  %t7 = load i1, %t0
  .loc 1 4 15
  %t8 = trunc1 1
  .loc 1 4 15
  %t9 = alloca 1
L50:
  .loc 1 5 10
  %t14 = trunc1 0
  .loc 1 5 20
  %t15 = load i1, %t0
  .loc 1 5 20
  %t16 = alloca 1
  .loc 1 5 16
  cbr %t14, bool_then1, bool_else1
exit:
  ret 0
bool_then:
bool_else:
  .loc 1 4 12
  store i1, %t9, %t8
  .loc 1 4 12
  br bool_join
bool_join:
  .loc 1 4 12
  %t11 = load i1, %t9
  .loc 1 4 4
  %t12 = zext1 %t11
  .loc 1 4 4
  call @rt_print_i64(%t12)
  .loc 1 4 4
  %t13 = const_str @.L0
  .loc 1 4 4
  call @rt_print_str(%t13)
  .loc 1 4 4
  br L50
bool_then1:
  .loc 1 5 16
  store i1, %t16, %t15
  .loc 1 5 16
  br bool_join1
bool_else1:
  .loc 1 5 16
  %t17 = trunc1 0
  .loc 1 5 16
  store i1, %t16, %t17
  .loc 1 5 16
  br bool_join1
bool_join1:
  .loc 1 5 16
  %t18 = load i1, %t16
  .loc 1 5 4
  %t19 = zext1 %t18
  .loc 1 5 4
  call @rt_print_i64(%t19)
  .loc 1 5 4
  %t20 = const_str @.L0
  .loc 1 5 4
  call @rt_print_str(%t20)
  .loc 1 5 4
  br exit
}
