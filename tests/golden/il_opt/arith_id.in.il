il 0.1
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %t0 = add 40, 2
  %t1 = add %t0, 0
  %t2 = sub %t1, 0
  %t3 = mul %t2, 1
  %t4 = and %t3, -1
  %t5 = or %t4, 0
  %t6 = xor %t5, 0
  call @rt_print_i64(%t6)
  ret 0
}
