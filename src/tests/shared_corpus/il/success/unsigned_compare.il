il 0.2.0

func @main() -> i64 {
entry:
  %cmp = ucmp_gt -1, 1
  %value = zext1 %cmp
  ret %value
}
