il 0.2.0
func @duplicate_param() -> void {
entry:
  br next(1, 2)
next(%x: i64, %x: i64):
  ret
}
