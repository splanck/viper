il 0.1
func @add2(%a:i64, %b:i64) -> i64 {
entry(%a:i64, %b:i64):
  %t0 = add %a, %b
  ret %t0
}
