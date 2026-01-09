il 0.2.0
extern @rt_fix_trunc(f64) -> f64
extern @rt_int_floor(f64) -> f64
extern @rt_print_f64(f64) -> void
extern @rt_round_even(f64, i32) -> f64
func @main() -> i64 {
entry:
  %t0 = call @rt_int_floor(1.9)
  %t1 = call @rt_int_floor(-1.1)
  %t2 = call @rt_fix_trunc(1.9)
  %t3 = call @rt_fix_trunc(-1.9)
  %t4 = call @rt_round_even(2.5, 0)
  %t5 = call @rt_round_even(3.5, 0)
  %t6 = call @rt_round_even(-2.5, 0)
  %t7 = call @rt_round_even(-3.5, 0)
  %t8 = call @rt_round_even(2.25, 1)
  %t9 = call @rt_round_even(2.35, 1)
  call @rt_print_f64(%t0)
  call @rt_print_f64(%t1)
  call @rt_print_f64(%t2)
  call @rt_print_f64(%t3)
  call @rt_print_f64(%t4)
  call @rt_print_f64(%t5)
  call @rt_print_f64(%t6)
  call @rt_print_f64(%t7)
  call @rt_print_f64(%t8)
  call @rt_print_f64(%t9)
  ret 0
}
