il 0.1.3
func @main() -> i64 {
entry:
  br L1(0)
L1(%t6:i64):
  %t2 = iadd.ovf %t6, 1
  %t3 = scmp_lt %t2, 10
  cbr %t3, L1(%t2), Exit(%t2)
Exit(%t7:i64):
  ret %t7
}
