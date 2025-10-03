il 0.1.2
func @unterminated() -> void {
entry:
  ret
// Intentionally missing the closing brace to exercise EOF diagnostics.
