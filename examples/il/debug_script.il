il 0.1
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  br L3
L3:
  %t0 = add 1, 2
  call @rt_print_i64(%t0)
  %t1 = add %t0, 3
  call @rt_print_i64(%t1)
  ret 0
}
