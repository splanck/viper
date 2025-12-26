il 0.1.3
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  store i64 %t0, 40
  store i64 %t1, 2
  %t2 = load i64 %t0
  %t3 = load i64 %t1
  %t4 = iadd.ovf %t2, %t3
  ret %t4
}
