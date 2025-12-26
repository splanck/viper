il 0.1.3
func @f(%arg:i32) -> void {
entry(%arg:i32):
  switch.i32 %arg, ^default(%arg) junk, 0 -> ^case0(%arg)
case0(%case_val:i32):
  ret
default(%default_val:i32):
  ret
}
