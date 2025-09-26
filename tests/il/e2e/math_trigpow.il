il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_f64(f64) -> void
extern @rt_sin(f64) -> f64
extern @rt_cos(f64) -> f64
extern @rt_pow_f64_chkdom(f64, f64) -> f64

global const str @.nl = "\n"

func @main() -> i64 {
entry:
  %s = call @rt_sin(0.0)
  call @rt_print_f64(%s)
  %nl0 = const_str @.nl
  call @rt_print_str(%nl0)
  %c = call @rt_cos(0.0)
  call @rt_print_f64(%c)
  %nl1 = const_str @.nl
  call @rt_print_str(%nl1)
  %p = call @rt_pow_f64_chkdom(2.0, 10.0)
  call @rt_print_f64(%p)
  %nl2 = const_str @.nl
  call @rt_print_str(%nl2)
  ret 0
}
