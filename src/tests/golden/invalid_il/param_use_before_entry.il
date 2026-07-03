il 0.3.0
func @param_use_before_entry() -> void {
entry:
  %a = iadd.ovf %p, %p
  br next(1)
next(%p: i64):
  %b = iadd.ovf %p, %p
  ret
}
