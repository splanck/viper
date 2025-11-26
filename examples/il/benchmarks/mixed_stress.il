il 0.1.2

func @helper(i64 %x) -> i64 {
entry(%x0: i64):
  %t1 = imul.ovf %x0, 3
  %t2 = iadd.ovf %t1, 7
  ret %t2
}

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 100000
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  %t1 = iadd.ovf %i0, 1
  %t2 = imul.ovf %t1, 2
  %t3 = isub.ovf %t2, %i0
  %mod4 = srem.chk0 %i0, 4
  %is_mod4 = icmp_eq %mod4, 0
  cbr %is_mod4, path_a(%sum0, %i0, %t3), path_b(%sum0, %i0, %t3)

path_a(%asum: i64, %ai: i64, %at: i64):
  %r1 = call @helper(%at)
  %r2 = imul.ovf %r1, 2
  br merge(%asum, %ai, %r2)

path_b(%bsum: i64, %bi: i64, %bt: i64):
  %r3 = iadd.ovf %bt, 100
  %r4 = imul.ovf %r3, 3
  br merge(%bsum, %bi, %r4)

merge(%msum: i64, %mi: i64, %tmp: i64):
  %mod7 = srem.chk0 %mi, 7
  %is_mod7 = icmp_eq %mod7, 0
  cbr %is_mod7, bonus(%msum, %mi, %tmp), accumulate(%msum, %mi, %tmp)

bonus(%bsum2: i64, %bi2: i64, %btmp: i64):
  %bonus_val = call @helper(%btmp)
  %with_bonus = iadd.ovf %btmp, %bonus_val
  br accumulate(%bsum2, %bi2, %with_bonus)

accumulate(%asum2: i64, %ai2: i64, %atmp: i64):
  %new_sum = iadd.ovf %asum2, %atmp
  %next_i = iadd.ovf %ai2, 1
  br loop(%new_sum, %next_i)

exit(%result: i64):
  ret %result
}
