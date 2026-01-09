il 0.2.0
func @main() -> i64 {
entry:
  %t0 = alloca 8
  store i64 %t0, 0
  br L1
L1:
  %t1 = load i64 %t0
  %t2 = iadd.ovf %t1, 1
  store i64 %t0, %t2
  %t3 = scmp_lt %t2, 10
  cbr %t3, L1, Exit
Exit:
  %t4 = load i64 %t0
  ret %t4
}
