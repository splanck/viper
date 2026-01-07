il 0.1.3
func @duplicate_block_param() -> void {
bb0(%x: i32, %x: i32):
  ret
}
