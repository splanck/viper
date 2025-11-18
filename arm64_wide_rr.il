il 0.1
func @f(%a:i64, %b:i64, %c:i64) -> i64 {
entry(%a:i64, %b:i64, %c:i64):
  %t0 = add %c, %a
  ret %t0
}
