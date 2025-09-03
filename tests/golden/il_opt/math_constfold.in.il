il 0.1.2
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_print_str(str) -> void
extern @rt_abs_i64(i64) -> i64
extern @rt_abs_f64(f64) -> f64
extern @rt_floor(f64) -> f64
extern @rt_ceil(f64) -> f64
extern @rt_sqrt(f64) -> f64
extern @rt_pow(f64, f64) -> f64
extern @rt_sin(f64) -> f64
extern @rt_cos(f64) -> f64
global const str @.nl = "\n"
func @main() -> i64 {
entry:
  %i = call @rt_abs_i64(-5)
  %f = call @rt_abs_f64(-3.5)
  %fl = call @rt_floor(3.7)
  %ce = call @rt_ceil(3.2)
  %sq = call @rt_sqrt(9.0)
  %pw = call @rt_pow(2.0, 10.0)
  %si = call @rt_sin(0.0)
  %co = call @rt_cos(0.0)
  call @rt_print_i64(%i)
  %nl0 = const_str @.nl
  call @rt_print_str(%nl0)
  call @rt_print_f64(%f)
  %nl1 = const_str @.nl
  call @rt_print_str(%nl1)
  call @rt_print_f64(%fl)
  %nl2 = const_str @.nl
  call @rt_print_str(%nl2)
  call @rt_print_f64(%ce)
  %nl3 = const_str @.nl
  call @rt_print_str(%nl3)
  call @rt_print_f64(%sq)
  %nl4 = const_str @.nl
  call @rt_print_str(%nl4)
  call @rt_print_f64(%pw)
  %nl5 = const_str @.nl
  call @rt_print_str(%nl5)
  call @rt_print_f64(%si)
  %nl6 = const_str @.nl
  call @rt_print_str(%nl6)
  call @rt_print_f64(%co)
  %nl7 = const_str @.nl
  call @rt_print_str(%nl7)
  ret 0
}
