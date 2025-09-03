il 0.1
extern @rt_randomize_i64(i64) -> void
extern @rt_rnd() -> f64
extern @rt_print_f64(f64) -> void
extern @rt_print_str(str) -> void

global const str @.L0 = "\n"

func @main() -> i32 {
entry:
  call @rt_randomize_i64(42)
  %r0 = call @rt_rnd()
  call @rt_print_f64(%r0)
  %nl0 = const_str @.L0
  call @rt_print_str(%nl0)
  %r1 = call @rt_rnd()
  call @rt_print_f64(%r1)
  %nl1 = const_str @.L0
  call @rt_print_str(%nl1)
  %r2 = call @rt_rnd()
  call @rt_print_f64(%r2)
  %nl2 = const_str @.L0
  call @rt_print_str(%nl2)
  ret 0
}
