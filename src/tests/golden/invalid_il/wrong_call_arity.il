il 0.1
extern @foo(i64) -> void
func @wrong_call() -> void {
entry:
  call @foo()
  ret
}
