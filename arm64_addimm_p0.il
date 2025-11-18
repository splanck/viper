il 0.1
func @f(%a:i64, %b:i64) -> i64 {
entry(%a:i64, %b:i64):
  %t0 = add %a, 5
  ret %t0
}
