il 0.1.2
func @main() -> i64 {
entry:
  br L1(0)
L1(%t5:i64):
  %t2 = iadd.ovf %t5, 1
  %t3 = scmp_lt %t2, 10
  cbr %t3, L1(%t2), Exit(%t2)
Exit(%t6:i64):
  ret %t6
}
