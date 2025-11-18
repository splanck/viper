il 0.1
func @g(%a:i64, %b:i64, %c:i64, %d:i64) -> i64 {
entry(%a:i64, %b:i64, %c:i64, %d:i64):
  %t0 = sub %d, 7
  ret %t0
}
