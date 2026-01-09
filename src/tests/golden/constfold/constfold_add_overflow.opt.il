il 0.2.0
func @main() -> i64 {
entry:
  %t0 = iadd.ovf 9223372036854775807, 1
  ret %t0
}
