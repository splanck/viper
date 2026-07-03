il 0.3.0
func @select_kinds(%c:i1, %a:i64, %b:i64, %x:f64, %y:f64) -> i64 {
entry(%c:i1, %a:i64, %b:i64, %x:f64, %y:f64):
  %t0 = select i64, %c, %a, %b
  %t1 = select f64, %c, %x, %y
  %t2 = select i64, %c, %t0, 7
  ret %t2
}
