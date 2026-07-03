il 0.3.0

func @main() -> i64 {
entry:
  %sum = iadd.ovf 9223372036854775807, 1
  ret %sum
}
