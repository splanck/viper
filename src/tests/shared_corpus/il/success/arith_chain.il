il 0.3.0

func @main() -> i64 {
entry:
  %a = iadd.ovf 17, 25
  %b = imul.ovf %a, 3
  %c = isub.ovf %b, 4
  %d = sdiv.chk0 %c, 2
  %e = srem.chk0 %d, 11
  ret %e
}
