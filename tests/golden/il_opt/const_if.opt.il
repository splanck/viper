il 0.1
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %t0 = scmp_lt 0, 1
  br label L1
L1:
  call @rt_print_i64(1)
  br label exit
L2:
  call @rt_print_i64(2)
  br label exit
exit:
  ret 0
}
