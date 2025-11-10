il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
extern @rt_abs_i64(i64) -> i64
extern @rt_abs_f64(f64) -> f64
extern @rt_floor(f64) -> f64
extern @rt_ceil(f64) -> f64
extern @rt_sqrt(f64) -> f64
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 10
  %t0 = call @rt_abs_i64(-5)
  .loc 1 1 4
  call @rt_print_i64(%t0)
  .loc 1 1 4
  %t1 = const_str @.L0
  .loc 1 1 4
  call @rt_print_str(%t1)
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t2 = call @rt_abs_f64(-1.5)
  .loc 1 2 4
  call @rt_print_f64(%t2)
  .loc 1 2 4
  %t3 = const_str @.L0
  .loc 1 2 4
  call @rt_print_str(%t3)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 10
  %t4 = call @rt_floor(1.8999999999999999)
  .loc 1 3 4
  call @rt_print_f64(%t4)
  .loc 1 3 4
  %t5 = const_str @.L0
  .loc 1 3 4
  call @rt_print_str(%t5)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 10
  %t6 = call @rt_ceil(1.1000000000000001)
  .loc 1 4 4
  call @rt_print_f64(%t6)
  .loc 1 4 4
  %t7 = const_str @.L0
  .loc 1 4 4
  call @rt_print_str(%t7)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 10
  %t8 = call @rt_sqrt(9)
  .loc 1 5 4
  call @rt_print_f64(%t8)
  .loc 1 5 4
  %t9 = const_str @.L0
  .loc 1 5 4
  call @rt_print_str(%t9)
  .loc 1 5 4
  br exit
exit:
  ret 0
}
