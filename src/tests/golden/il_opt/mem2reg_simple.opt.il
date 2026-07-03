il 0.3.0
func @main() -> i64 {
entry:
  %t3 = iadd.ovf 42, 42
  ret %t3
}
