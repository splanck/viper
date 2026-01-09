il 0.2.0
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %t0 = iadd.ovf 40, 2
  %t1 = iadd.ovf %t0, 0
  call @rt_print_i64(%t1)
  ret 0
}
