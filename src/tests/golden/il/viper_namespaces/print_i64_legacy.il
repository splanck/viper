il 0.1.3
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  call @rt_print_i64(42)
  ret 0
}

