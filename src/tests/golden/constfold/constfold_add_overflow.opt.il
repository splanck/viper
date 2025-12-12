il 0.1.3
func @main() -> i64 {
entry:
  %t0 = iadd.ovf 9223372036854775807, 1
  ret %t0
}
