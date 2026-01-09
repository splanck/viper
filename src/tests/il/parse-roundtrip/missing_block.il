il 0.2.0
func @unresolved() -> void {
entry:
  // Branch to an undefined block; parser should report "unknown block".
  br missing()
}
