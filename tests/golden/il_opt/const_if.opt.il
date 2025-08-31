il 0.1
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  br label L1
L1:
  call @rt_print_i64(1)
  br label exit
exit:
  ret 0
}
