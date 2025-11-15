il 0.1
func @bad_mem() -> void {
entry:
  %i = iadd.ovf 1, 2
  %p = const_null
  store i64, %i, %p
  ret
}
