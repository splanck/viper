il 0.2.0
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %t0 = scmp_lt 0, 1
  cbr %t0, L1, L2
L1:
  call @rt_print_i64(1)
  br exit
L2:
  call @rt_print_i64(2)
  br exit
exit:
  ret 0
}
