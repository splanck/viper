il 0.1.2
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %t0 = iadd.ovf 9223372036854775807, 1
  call @rt_print_i64(%t0)
  ret 0
}
