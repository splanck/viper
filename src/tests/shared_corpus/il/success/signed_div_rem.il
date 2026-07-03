il 0.3.0

func @main() -> i64 {
entry:
  %q = sdiv.chk0 -22, 5
  %r = srem.chk0 -22, 5
  %value = iadd.ovf %q, %r
  ret %value
}
