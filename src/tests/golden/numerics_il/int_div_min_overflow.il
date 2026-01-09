il 0.2.0
func @main() -> i64 {
entry:
  %t0:i16 = sdiv.chk0 -32768, -1
  ret 0
}
