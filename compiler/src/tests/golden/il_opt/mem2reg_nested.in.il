il 0.1.3
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %c1 = icmp_eq 0, 0
  cbr %c1, T1, F1
T1:
  store i64 %t0, 2
  br Join
F1:
  %c2 = icmp_eq 0, 0
  cbr %c2, T2, F2
T2:
  store i64 %t0, 3
  br Join
F2:
  store i64 %t0, 4
  br Join
Join:
  %t1 = load i64 %t0
  ret %t1
}
