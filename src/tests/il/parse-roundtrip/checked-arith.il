il 0.1.3
func @checked_arith(%lhs:i64, %rhs:i64) -> i64 {
entry(%lhs:i64, %rhs:i64):
  %t0 = iadd.ovf %lhs, %rhs
  %t1 = isub.ovf %lhs, %rhs
  %t2 = imul.ovf %lhs, %rhs
  ret %t0
}
