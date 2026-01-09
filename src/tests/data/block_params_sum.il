il 0.2.0

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%i: i64, %acc: i64):
  %cond = scmp_ge %i, 10
  cbr %cond, exit(%acc), body(%i, %acc)

body(%i: i64, %acc: i64):
  %i1 = iadd.ovf %i, 1
  %acc1 = iadd.ovf %acc, %i1
  br loop(%i1, %acc1)

exit(%res: i64):
  ret %res
}
