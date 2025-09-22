il 0.1
func @bad_types() -> i64 {
entry:
  %p = const_null
  %r = add %p, 1
  ret 0
}
