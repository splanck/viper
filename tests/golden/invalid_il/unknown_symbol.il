il 0.1
func @unknown_symbol() -> void {
entry:
  call @does_not_exist()
  ret
}
