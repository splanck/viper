il 0.3.0

func @main() -> i64 {
entry:
  %q = sdiv.chk0 1, 0
  ret %q
}
