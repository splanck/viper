il 0.2.0
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_abs_i64(i64) -> i64
extern @rt_abs_f64(f64) -> f64
extern @rt_sqrt(f64) -> f64
extern @rt_pow_f64_chkdom(f64, f64) -> f64
extern @rt_sin(f64) -> f64
func @main() -> i64 {
entry:
  %t0 = call @rt_abs_i64(-5)
  %t1 = call @rt_abs_f64(-1.5)
  %t2 = call @rt_sqrt(2.0)
  %t3 = call @rt_pow_f64_chkdom(2.0, -1.0)
  %t4 = call @rt_sin(0.0)
  call @rt_print_i64(%t0)
  call @rt_print_f64(%t1)
  call @rt_print_f64(%t2)
  call @rt_print_f64(%t3)
  call @rt_print_f64(%t4)
  ret 0
}
