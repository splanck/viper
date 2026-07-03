il 0.3.0

func @main() -> i64 {
entry:
  %a = shl 5, 4
  %b = xor %a, 12
  %c = or %b, 3
  %d = and %c, 127
  ret %d
}
