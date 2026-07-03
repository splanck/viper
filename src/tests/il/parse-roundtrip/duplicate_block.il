il 0.3.0
func @duplicate_block() -> void {
entry:
  br next()

next:
  ret
next:
  ret
}
