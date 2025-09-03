il 0.1.2

func @sum_to_three() -> i64 {
entry:
  br loop(0, 0)

loop(%i: i64, %acc: i64):
  %cond = scmp_ge %i, 3
  cbr %cond, exit(%acc), body(%i, %acc)

body(%i: i64, %acc: i64):
  %acc1 = add %acc, %i
  %i1 = add %i, 1
  br loop(%i1, %acc1)

exit(%result: i64):
  ret %result
}
