il 0.1.2
extern @rt_fix_trunc(f64) -> f64
extern @rt_int_floor(f64) -> f64
extern @rt_print_f64(f64) -> void
extern @rt_round_even(f64, i64) -> f64
func @main() -> i64 {
entry:
  call @rt_print_f64(2.0)
  call @rt_print_f64(-2.0)
  call @rt_print_f64(2.0)
  call @rt_print_f64(-1.0)
  call @rt_print_f64(2.0)
  call @rt_print_f64(4.0)
  call @rt_print_f64(1.23)
  call @rt_print_f64(20.0)
  ret 0
}
