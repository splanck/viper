il 0.1.2

func @unchecked_srem(%a:i64, %b:i64) -> i64 {
entry(%a:i64, %b:i64):
  %t0 = srem %a, %b
  ret %t0
}
