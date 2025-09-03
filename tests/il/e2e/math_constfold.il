il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_abs_i64(i64) -> i64
extern @rt_abs_f64(f64) -> f64
extern @rt_sqrt(f64) -> f64
extern @rt_pow(f64, f64) -> f64
extern @rt_sin(f64) -> f64
global const str @.nl = "\n"
func @main() -> i64 {
entry:
  %i = call @rt_abs_i64(-5)
  call @rt_print_i64(%i)
  %nl0 = const_str @.nl
  call @rt_print_str(%nl0)
  %f = call @rt_abs_f64(-3.5)
  call @rt_print_f64(%f)
  %nl1 = const_str @.nl
  call @rt_print_str(%nl1)
  %sq = call @rt_sqrt(2.0)
  call @rt_print_f64(%sq)
  %nl2 = const_str @.nl
  call @rt_print_str(%nl2)
  %pw = call @rt_pow(2.5, 2.0)
  call @rt_print_f64(%pw)
  %nl3 = const_str @.nl
  call @rt_print_str(%nl3)
  %si = call @rt_sin(0.0)
  call @rt_print_f64(%si)
  %nl4 = const_str @.nl
  call @rt_print_str(%nl4)
  ret 0
}
