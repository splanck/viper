il 0.2.0
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_abs_i64(i64) -> i64
extern @rt_abs_f64(f64) -> f64
extern @rt_floor(f64) -> f64
extern @rt_ceil(f64) -> f64
extern @rt_sqrt(f64) -> f64
extern @rt_pow_f64_chkdom(f64, f64) -> f64
extern @rt_sin(f64) -> f64
extern @rt_cos(f64) -> f64
func @main() -> i64 {
entry:
  %t0 = call @rt_abs_i64(-5)
  %t1 = call @rt_abs_f64(-1.5)
  %t2 = call @rt_floor(1.9)
  %t3 = call @rt_ceil(1.1)
  %t4 = call @rt_sqrt(9.0)
  %t5 = call @rt_pow_f64_chkdom(2.0, 10.0)
  %t6 = call @rt_sin(0.0)
  %t7 = call @rt_cos(0.0)
  call @rt_print_i64(%t0)
  call @rt_print_f64(%t1)
  call @rt_print_f64(%t2)
  call @rt_print_f64(%t3)
  call @rt_print_f64(%t4)
  call @rt_print_f64(%t5)
  call @rt_print_f64(%t6)
  call @rt_print_f64(%t7)
  ret 0
}
