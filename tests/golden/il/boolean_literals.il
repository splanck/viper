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
  %t0 = alloca 8
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
  %t6 = zext1 %t5
  .loc 1 3 4
  store i64, %t0, %t6
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 10
  %t7 = load i64, %t0
  .loc 1 4 12
  %t8 = alloca 1
L50:
  .loc 1 5 16
  %t12 = alloca 1
  .loc 1 5 16
  cbr 0, and_rhs, and_false
exit:
  ret 0
or_true:
or_rhs:
  .loc 1 4 12
  store i1, %t8, 1
  .loc 1 4 12
  br or_done
or_done:
  .loc 1 4 12
  %t9 = load i1, %t8
  .loc 1 4 4
  %t10 = zext1 %t9
  .loc 1 4 4
  call @rt_print_i64(%t10)
  .loc 1 4 4
  %t11 = const_str @.L0
  .loc 1 4 4
  call @rt_print_str(%t11)
  .loc 1 4 4
  br L50
and_rhs:
  .loc 1 5 20
  %t13 = load i64, %t0
  .loc 1 5 16
  store i1, %t12, %t13
  .loc 1 5 16
  br and_done
and_false:
  .loc 1 5 16
  store i1, %t12, 0
  .loc 1 5 16
  br and_done
and_done:
  .loc 1 5 16
  %t14 = load i1, %t12
  .loc 1 5 4
  %t15 = zext1 %t14
  .loc 1 5 4
  call @rt_print_i64(%t15)
  .loc 1 5 4
  %t16 = const_str @.L0
  .loc 1 5 4
  call @rt_print_str(%t16)
  .loc 1 5 4
  br exit
}
