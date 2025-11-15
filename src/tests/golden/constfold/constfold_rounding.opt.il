il 0.1.2
extern @rt_fix_trunc(f64) -> f64
extern @rt_int_floor(f64) -> f64
extern @rt_print_f64(f64) -> void
extern @rt_round_even(f64, i32) -> f64
func @main() -> i64 {
entry:
  call @rt_print_f64(1)
  call @rt_print_f64(-2)
  call @rt_print_f64(1)
  call @rt_print_f64(-1)
  call @rt_print_f64(2)
  call @rt_print_f64(4)
  call @rt_print_f64(-2)
  call @rt_print_f64(-4)
  call @rt_print_f64(2.2000000000000002)
  call @rt_print_f64(2.3999999999999999)
  ret 0
}
