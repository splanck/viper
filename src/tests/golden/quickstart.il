il 0.1.2
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  call @rt_print_i64(4)
  ret 0
}
