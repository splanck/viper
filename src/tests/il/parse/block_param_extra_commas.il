il 0.2.0
func @comma_block(i32 %arg0) -> i32 {
entry(%x: i32,, %y: i32):
  ret %arg0
}
