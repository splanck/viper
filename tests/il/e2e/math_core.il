il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_f64(f64) -> void
extern @rt_print_i64(i64) -> void
extern @rt_sqrt(f64) -> f64
extern @rt_floor(f64) -> f64
extern @rt_ceil(f64) -> f64
extern @rt_abs_i64(i64) -> i64
extern @rt_abs_f64(f64) -> f64

global const str @.nl = "\n"

func @main() -> i64 {
entry:
  %s = call @rt_sqrt(9.0)
  call @rt_print_f64(%s)
  %nl0 = const_str @.nl
  call @rt_print_str(%nl0)
  %f = call @rt_floor(3.7)
  call @rt_print_f64(%f)
  %nl1 = const_str @.nl
  call @rt_print_str(%nl1)
  %c = call @rt_ceil(3.2)
  call @rt_print_f64(%c)
  %nl2 = const_str @.nl
  call @rt_print_str(%nl2)
  %ai = call @rt_abs_i64(-42)
  call @rt_print_i64(%ai)
  %nl3 = const_str @.nl
  call @rt_print_str(%nl3)
  %af = call @rt_abs_f64(-3.5)
  call @rt_print_f64(%af)
  %nl4 = const_str @.nl
  call @rt_print_str(%nl4)
  ret 0
}
