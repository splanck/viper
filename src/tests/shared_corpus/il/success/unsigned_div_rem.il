il 0.3.0

func @main() -> i64 {
entry:
  %q = udiv.chk0 100, 9
  %r = urem.chk0 100, 9
  %value = iadd.ovf %q, %r
  ret %value
}
