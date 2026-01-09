il 0.2.0

func @main() -> i64 {
entry:
  br loop(0)

loop(%i:i64):
  %c = scmp_lt %i, 10
  cbr %c, body(%i), exit

body(%x:i64):
  %n = iadd.ovf %x, 1
  br loop(%n)

exit:
  ret 0
}
