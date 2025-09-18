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
  %t1 = alloca 1
  store i1, %t1, 0
  %t2 = alloca 8
  %t3 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t4 = call @rt_input_line()
  .loc 1 1 4
  %t5 = call @rt_to_int(%t4)
  .loc 1 1 4
  store i64, %t3, %t5
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  %t6 = call @rt_input_line()
  .loc 1 2 4
  %t7 = call @rt_to_int(%t6)
  .loc 1 2 4
  store i64, %t2, %t7
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 14
  %t8 = load i64, %t3
  .loc 1 5 16
  %t9 = icmp_ne %t8, 0
  .loc 1 5 4
  store i1, %t1, %t9
  .loc 1 5 4
  br L60
L60:
  .loc 1 6 17
  %t10 = load i1, %t1
  .loc 1 6 25
  %t11 = load i64, %t2
  .loc 1 6 27
  %t12 = icmp_ne %t11, 0
  .loc 1 6 27
  %t13 = alloca 1
  .loc 1 6 21
  cbr %t10, bool_then, bool_else
L70:
  .loc 1 7 10
  %t15 = load i1, %t0
  .loc 1 7 4
  %t16 = zext1 %t15
  .loc 1 7 4
  call @rt_print_i64(%t16)
  .loc 1 7 4
  %t17 = const_str @.L0
  .loc 1 7 4
  call @rt_print_str(%t17)
  .loc 1 7 4
  br exit
exit:
  ret 0
bool_then:
  .loc 1 6 21
  store i1, %t13, 1
  .loc 1 6 21
  br bool_join
bool_else:
  .loc 1 6 21
  store i1, %t13, %t12
  .loc 1 6 21
  br bool_join
bool_join:
  .loc 1 6 21
  %t14 = load i1, %t13
  .loc 1 6 4
  store i1, %t0, %t14
  .loc 1 6 4
  br L70
}
