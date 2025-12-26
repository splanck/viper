il 0.1.3
target "x86_64-unknown-linux-gnu"
extern @rt_print_str(str) -> void
global const str @.L0 = "HELLO"
func @main() -> i64 {
entry:
  %t0 = const_str @.L0
  call @rt_print_str(%t0)
  ret 0
}
