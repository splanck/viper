il 0.1.2
func @duplicate_block_param() -> void {
bb0(%x: i32, %x: i32):
  ret
}
