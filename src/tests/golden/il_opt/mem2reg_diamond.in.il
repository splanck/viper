il 0.1.2
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = icmp_eq 0, 0
  cbr %t1, T, F
T:
  store i64 %t0, 2
  br Join
F:
  store i64 %t0, 3
  br Join
Join:
  %t2 = load i64 %t0
  ret %t2
}
