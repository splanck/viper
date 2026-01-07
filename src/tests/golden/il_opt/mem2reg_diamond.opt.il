il 0.1.3
func @main() -> i64 {
entry:
  %t1 = icmp_eq 0, 0
  cbr %t1, T, F
T:
  br Join(2)
F:
  br Join(3)
Join(%t4:i64):
  ret %t4
}
