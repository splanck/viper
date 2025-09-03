il 0.1.2

extern @rt_randomize_i64(i64) -> void
extern @rt_rnd() -> f64
extern @rt_print_f64(f64) -> void

func @main() -> i64 {
entry:
  call @rt_randomize_i64(1)
  %r1 = call @rt_rnd()
  %r2 = call @rt_rnd()
  %r3 = call @rt_rnd()
  call @rt_print_f64(%r1)
  call @rt_print_f64(%r2)
  call @rt_print_f64(%r3)
  ret 0
}
