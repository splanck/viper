il 0.2.0

func @main() -> i64 {
entry:
  %nan = fdiv 0.0, 0.0
  %ord = fcmp_ord %nan, 2.0
  %value = zext1 %ord
  ret %value
}
