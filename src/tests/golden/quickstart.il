il 0.2.0
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  call @rt_print_i64(4)
  ret 0
}
