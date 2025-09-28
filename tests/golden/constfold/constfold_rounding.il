il 0.1.2
extern @rt_print_f64(f64) -> void
extern @rt_int_floor(f64) -> f64
extern @rt_fix_trunc(f64) -> f64
extern @rt_round_even(f64, i32) -> f64
func @main() -> i64 {
entry:
  %int_pos = call @rt_int_floor(2.7)
  %int_neg = call @rt_int_floor(-1.5)
  %fix_pos = call @rt_fix_trunc(2.7)
  %fix_neg = call @rt_fix_trunc(-1.5)
  %round_low = call @rt_round_even(2.5, 0)
  %round_high = call @rt_round_even(3.5, 0)
  call @rt_print_f64(%int_pos)
  call @rt_print_f64(%int_neg)
  call @rt_print_f64(%fix_pos)
  call @rt_print_f64(%fix_neg)
  call @rt_print_f64(%round_low)
  call @rt_print_f64(%round_high)
  ret 0
}
