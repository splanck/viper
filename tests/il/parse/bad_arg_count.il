il 0.1.2
func @f() -> i64 {
entry(%x:i64, %y:i64):
  ret 0
start:
  br entry(1)
}
