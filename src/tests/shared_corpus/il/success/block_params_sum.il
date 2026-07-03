il 0.3.0

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%i: i64, %acc: i64):
  %done = scmp_ge %i, 10
  cbr %done, exit(%acc), body(%i, %acc)

body(%i: i64, %acc: i64):
  %next = iadd.ovf %i, 1
  %sum = iadd.ovf %acc, %next
  br loop(%next, %sum)

exit(%result: i64):
  ret %result
}
