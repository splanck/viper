il 0.2.0

func @main() -> i64 {
entry:
  %slot = alloca 8
  store i64, %slot, 77
  %value = load i64, %slot
  ret %value
}
