il 0.2.0
func @duplicate_block_param() -> void {
bb0(%x: i32, %x: i32):
  ret
}
