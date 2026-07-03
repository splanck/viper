il 0.3.0
func @unterminated() -> void {
entry:
  ret
// Intentionally missing the closing brace to exercise EOF diagnostics.
