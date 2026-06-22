il 0.2.0

func @main() -> i64 {
entry:
  %nan = fdiv 0.0, 0.0
  %uno = fcmp_uno %nan, 2.0
  %value = zext1 %uno
  ret %value
}
