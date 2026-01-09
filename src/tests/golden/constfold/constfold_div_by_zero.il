il 0.2.0
func @main() -> i64 {
entry:
  %t0 = sdiv.chk0 1, 0
  ret %t0
}
