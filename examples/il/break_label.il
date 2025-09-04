il 0.1

func @main() -> i64 {
entry:
  br L3
L3:
  trap
  ret 0
}
