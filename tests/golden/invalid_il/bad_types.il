il 0.1
func @bad_types() -> i64 {
entry:
  %p = const_null
  %r = iadd.ovf %p, 1
  ret 0
}
