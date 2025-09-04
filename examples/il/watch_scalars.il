il 0.1

func @main() -> i64 {
entry:
  %x = alloca 8
  store i64, %x, 1
  store i64, %x, 1
  store i64, %x, 2
  ret 0
}
