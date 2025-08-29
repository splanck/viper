il 0.1
func @bad_mem() -> void {
entry:
  %i = add 1, 2
  %p = const_null
  store i64, %i, %p
  ret
}
