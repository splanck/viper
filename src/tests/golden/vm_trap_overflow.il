il 0.1.2
func @main() -> i64 {
entry:
  .loc 1 1 1
  %t0:i64 = iadd.ovf 9223372036854775807, 1
  ret %t0
}
