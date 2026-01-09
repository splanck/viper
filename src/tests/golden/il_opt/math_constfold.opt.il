il 0.2.0
extern @rt_abs_f64(f64) -> f64
extern @rt_abs_i64(i64) -> i64
extern @rt_ceil(f64) -> f64
extern @rt_cos(f64) -> f64
extern @rt_floor(f64) -> f64
extern @rt_pow_f64_chkdom(f64, f64) -> f64
extern @rt_print_f64(f64) -> void
extern @rt_print_i64(i64) -> void
extern @rt_sin(f64) -> f64
extern @rt_sqrt(f64) -> f64
func @main() -> i64 {
entry:
  call @rt_print_i64(5)
  call @rt_print_f64(1.5)
  call @rt_print_f64(1)
  call @rt_print_f64(2)
  call @rt_print_f64(3)
  call @rt_print_f64(1024)
  call @rt_print_f64(0.0)
  call @rt_print_f64(1)
  ret 0
}
