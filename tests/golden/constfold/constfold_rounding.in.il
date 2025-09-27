il 0.1.2
extern @rt_print_f64(f64) -> void
extern @rt_int_floor(f64) -> f64
extern @rt_fix_trunc(f64) -> f64
extern @rt_round_even(f64, i64) -> f64
func @main() -> i64 {
entry:
  %t0 = call @rt_int_floor(2.5)
  %t1 = call @rt_int_floor(-1.5)
  %t2 = call @rt_fix_trunc(2.5)
  %t3 = call @rt_fix_trunc(-1.5)
  %t4 = call @rt_round_even(2.5, 0)
  %t5 = call @rt_round_even(3.5, 0)
  %t6 = call @rt_round_even(1.2345, 2)
  %t7 = call @rt_round_even(25.0, -1)
  call @rt_print_f64(%t0)
  call @rt_print_f64(%t1)
  call @rt_print_f64(%t2)
  call @rt_print_f64(%t3)
  call @rt_print_f64(%t4)
  call @rt_print_f64(%t5)
  call @rt_print_f64(%t6)
  call @rt_print_f64(%t7)
  ret 0
}
