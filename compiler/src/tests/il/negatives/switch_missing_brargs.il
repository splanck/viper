il 0.1.3

func @switch_missing_brargs(%sel:i32, %x:i32) -> void {
entry(%sel:i32, %x:i32):
  switch.i32 %sel, ^default(%sel), 0 -> ^case0(%sel)
default(%d_sel:i32):
  ret
case0(%c_sel:i32):
  ret
}
