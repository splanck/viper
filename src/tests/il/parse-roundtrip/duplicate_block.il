il 0.1.2
func @duplicate_block() -> void {
entry:
  br next()

next:
  ret
next:
  ret
}
