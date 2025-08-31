il 0.1
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %t0 = add 40, 2
  %t1 = add %t0, 0
  call @rt_print_i64(%t1)
  ret 0
}
