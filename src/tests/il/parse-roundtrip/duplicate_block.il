il 0.2.0
func @duplicate_block() -> void {
entry:
  br next()

next:
  ret
next:
  ret
}
