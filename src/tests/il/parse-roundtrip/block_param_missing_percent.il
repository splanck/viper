il 0.1.3
func @block_param_missing_percent() -> i32 {
entry:
  br bb0()
bb0(x: i32):
  ret 0
}
