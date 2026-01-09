il 0.2.0
func @unterminated() -> void {
entry:
  ret
// Intentionally missing the closing brace to exercise EOF diagnostics.
