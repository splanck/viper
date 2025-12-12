il 0.1.3
func @idx_chk_demo(%idx16:i16, %lo16:i16, %hi16:i16, %idx32:i32, %lo32:i32, %hi32:i32) -> void {
entry(%idx16:i16, %lo16:i16, %hi16:i16, %idx32:i32, %lo32:i32, %hi32:i32):
  %t0 = idx.chk %idx16, %lo16, %hi16
  %t1 = idx.chk %idx32, %lo32, %hi32
  ret
}
