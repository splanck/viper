il 0.1.2
func @checked_divrem(%a:i64, %b:i64) -> i64 {
entry(%a:i64, %b:i64):
  %t0 = sdiv.chk0 %a, %b
  %t1 = srem.chk0 %a, %b
  %t2 = udiv.chk0 %a, %b
  %t3 = urem.chk0 %a, %b
  ret %t0
}
