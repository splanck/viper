il 0.1.2
func @main() -> i64 {
entry:
  %t0:i16 = iadd.ovf 32767, 1
  ret 0
}
