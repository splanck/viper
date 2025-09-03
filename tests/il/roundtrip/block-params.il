il 0.1.2

func @main() -> i32 {
entry:
  br loop(0)

loop(%i:i64):
  %c = scmp_lt %i, 10
  cbr %c, body(%i), exit

body(%x:i64):
  %n = add %x, 1
  br loop(%n)

exit:
  ret 0
}
