il 0.1

func @main() -> i64 {
entry:
  %x = alloca 8
  store i64, %x, 1
  store i64, %x, 1
  store f64, %x, 1.0
  store f64, %x, 2.0
  store i64, %x, 2
  store i64, %x, 2
  store f64, %x, 2.0
  store f64, %x, 3.0
  ret 0
}
