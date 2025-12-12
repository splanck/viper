il 0.1.3
func @wrong_block_arg_arity() -> void {
entry:
  br next(1)
next():
  ret
}
