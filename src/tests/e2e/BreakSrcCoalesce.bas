il 0.2.0
func @main() -> i64 {
entry:
  .loc 1 1 1
  %t0 = iadd.ovf 1, 2
  .loc 1 1 1
  %t1 = iadd.ovf %t0, 3
  .loc 1 1 1
  ret 0
}
