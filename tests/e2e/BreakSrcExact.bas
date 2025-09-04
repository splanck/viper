il 0.1.2
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  .loc 1 1 0
  call @rt_print_i64(1)
  .loc 1 2 0
  call @rt_print_i64(2)
  .loc 1 3 0
  call @rt_print_i64(3)
  ret 0
}
