il 0.2.0

func @main() -> i64 {
entry:
  %base = alloca 64
  %ptr = gep %base, 24
  store i64, %ptr, 99
  %value = load i64, %ptr
  ret %value
}
