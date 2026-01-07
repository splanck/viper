il 0.1.3
func @main() -> i64 {
entry:
  %c1 = icmp_eq 0, 0
  cbr %c1, T1, F1
T1:
  br Join(2)
F1:
  %c2 = icmp_eq 0, 0
  cbr %c2, T2, F2
T2:
  br Join(3)
F2:
  br Join(4)
Join(%t5:i64):
  ret %t5
}
