il 0.2.0
func @callee(%x:i64) -> void {
entry(%x:i64):
  ret
}
func @main() -> void {
entry:
  call @callee(1, 2)
  ret
}
