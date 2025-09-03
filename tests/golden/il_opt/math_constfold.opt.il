il 0.1.2
extern @rt_abs_f64(f64) -> f64
extern @rt_abs_i64(i64) -> i64
extern @rt_ceil(f64) -> f64
extern @rt_cos(f64) -> f64
extern @rt_floor(f64) -> f64
extern @rt_pow(f64, f64) -> f64
extern @rt_print_f64(f64) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_str(str) -> void
extern @rt_sin(f64) -> f64
extern @rt_sqrt(f64) -> f64
global const str @.nl = "\n"
func @main() -> i64 {
entry:
  call @rt_print_i64(5)
  %t8 = const_str @.nl
  call @rt_print_str(%t8)
  call @rt_print_f64(3.5)
  %t9 = const_str @.nl
  call @rt_print_str(%t9)
  call @rt_print_f64(3)
  %t10 = const_str @.nl
  call @rt_print_str(%t10)
  call @rt_print_f64(4)
  %t11 = const_str @.nl
  call @rt_print_str(%t11)
  call @rt_print_f64(3)
  %t12 = const_str @.nl
  call @rt_print_str(%t12)
  call @rt_print_f64(1024)
  %t13 = const_str @.nl
  call @rt_print_str(%t13)
  call @rt_print_f64(0.0)
  %t14 = const_str @.nl
  call @rt_print_str(%t14)
  call @rt_print_f64(1)
  %t15 = const_str @.nl
  call @rt_print_str(%t15)
  ret 0
}
