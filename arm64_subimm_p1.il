il 0.1
func @g(%a:i64, %b:i64) -> i64 {
entry(%a:i64, %b:i64):
  %t0 = sub %b, 3
  ret %t0
}
