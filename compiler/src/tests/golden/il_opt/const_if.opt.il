il 0.1.3
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  br L1
L1:
  call @rt_print_i64(1)
  br exit
L2:
  call @rt_print_i64(2)
  br exit
exit:
  ret 0
}
