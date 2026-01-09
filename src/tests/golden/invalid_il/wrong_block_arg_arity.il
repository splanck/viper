il 0.2.0
func @wrong_block_arg_arity() -> void {
entry:
  br next(1)
next():
  ret
}
