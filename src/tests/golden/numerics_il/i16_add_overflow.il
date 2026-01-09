il 0.2.0
func @main() -> i64 {
entry:
  %t0:i16 = iadd.ovf 32767, 1
  ret 0
}
