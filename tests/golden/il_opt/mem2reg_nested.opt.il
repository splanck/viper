il 0.1.2
func @main() -> i64 {
entry:
  %t1 = icmp_eq 0, 0
  cbr %t1, T1, F1
T1:
  br Join(2)
F1:
  %t2 = icmp_eq 0, 0
  cbr %t2, T2, F2
T2:
  br Join(3)
F2:
  br Join(4)
Join(%t4:i64):
  ret %t4
}
