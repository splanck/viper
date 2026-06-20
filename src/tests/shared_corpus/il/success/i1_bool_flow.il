il 0.2.0

func @main() -> i64 {
entry:
  %cmp = scmp_gt 12, 7
  %flag = zext1 %cmp
  ret %flag
}
