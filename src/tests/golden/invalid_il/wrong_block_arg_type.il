il 0.2.0
func @wrong_block_arg_type() -> void {
entry:
  br next(1.0)
next(%x: i64):
  ret
}
