il 0.1.3
func @main() -> i64 {
entry:
  %t0 = alloca 8
  store i64 %t0, 42
  %t1 = load i64 %t0
  %t2 = load i64 %t0
  %t3 = iadd.ovf %t1, %t2
  ret %t3
}
