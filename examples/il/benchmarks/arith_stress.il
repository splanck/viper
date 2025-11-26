il 0.1.2

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 500000
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  %t1 = iadd.ovf %i0, 1
  %t2 = imul.ovf %t1, 2
  %t3 = iadd.ovf %i0, 3
  %t4 = iadd.ovf %t2, %t3
  %t5 = imul.ovf %t4, 5
  %t6 = isub.ovf %t5, %i0
  %t7 = iadd.ovf %t6, 7
  %t8 = imul.ovf %t7, 3
  %t9 = isub.ovf %t8, 11
  %new_sum = iadd.ovf %sum0, %t9
  %next_i = iadd.ovf %i0, 1
  br loop(%new_sum, %next_i)

exit(%result: i64):
  ret %result
}
