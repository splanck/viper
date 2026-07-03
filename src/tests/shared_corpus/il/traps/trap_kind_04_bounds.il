il 0.3.0

func @main() -> i64 {
entry:
  %idx:i64 = idx.chk 20, 10, 20
  ret %idx
}
