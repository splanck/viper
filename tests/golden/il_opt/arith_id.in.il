il 0.1.2
extern @rt_print_i64(i64) -> void
func @main() -> i64 {
entry:
  %t0 = iadd.ovf 40, 2
  %t1 = iadd.ovf %t0, 0
  %t2 = isub.ovf %t1, 0
  %t3 = imul.ovf %t2, 1
  %t4 = and %t3, -1
  %t5 = or %t4, 0
  %t6 = xor %t5, 0
  call @rt_print_i64(%t6)
  ret 0
}
