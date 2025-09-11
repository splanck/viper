il 0.1
extern @rt_print_str(str) -> void
func @main() -> i32 {
entry:
  call @rt_print_str(42)
  ret 0
}

