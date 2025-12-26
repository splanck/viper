il 0.1.3
func @comma_block(i32 %arg0) -> i32 {
entry(%x: i32,, %y: i32):
  ret %arg0
}
