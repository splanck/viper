il 0.1.2
func @unresolved() -> void {
entry:
  // Branch to an undefined block; parser should report "unknown block".
  br missing()
}
