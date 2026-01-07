il 0.1.3
func @param_use_before_entry() -> void {
entry:
  %a = iadd.ovf %t0, %t0
  br next(1)
next(%p: i64):
  %b = iadd.ovf %p, %p
  ret
}
