il 0.2.0

// switch_dispatch: dense-switch lowering litmus.
// A 16-case dense switch.i32 dispatched 50M times with a rotating key.
// A binary compare tree pays log2(16) = 4 compares + branches per
// dispatch; a jump table pays one bounds check + one indirect branch.
// Expected exit-code checksum: 232

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 50000000
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  %k64 = and %i0, 15
  %k:i32 = cast.si_narrow.chk %k64
  switch.i32 %k, ^dflt(%sum0, %i0), 0 -> ^c0(%sum0, %i0), 1 -> ^c1(%sum0, %i0), 2 -> ^c2(%sum0, %i0), 3 -> ^c3(%sum0, %i0), 4 -> ^c4(%sum0, %i0), 5 -> ^c5(%sum0, %i0), 6 -> ^c6(%sum0, %i0), 7 -> ^c7(%sum0, %i0), 8 -> ^c8(%sum0, %i0), 9 -> ^c9(%sum0, %i0), 10 -> ^c10(%sum0, %i0), 11 -> ^c11(%sum0, %i0), 12 -> ^c12(%sum0, %i0), 13 -> ^c13(%sum0, %i0), 14 -> ^c14(%sum0, %i0), 15 -> ^c15(%sum0, %i0)

c0(%s: i64, %j: i64):
  %v0 = iadd.ovf %s, 2
  br join(%v0, %j)

c1(%s: i64, %j: i64):
  %v1 = iadd.ovf %s, 3
  br join(%v1, %j)

c2(%s: i64, %j: i64):
  %v2 = iadd.ovf %s, 5
  br join(%v2, %j)

c3(%s: i64, %j: i64):
  %v3 = iadd.ovf %s, 7
  br join(%v3, %j)

c4(%s: i64, %j: i64):
  %v4 = iadd.ovf %s, 11
  br join(%v4, %j)

c5(%s: i64, %j: i64):
  %v5 = iadd.ovf %s, 13
  br join(%v5, %j)

c6(%s: i64, %j: i64):
  %v6 = iadd.ovf %s, 17
  br join(%v6, %j)

c7(%s: i64, %j: i64):
  %v7 = iadd.ovf %s, 19
  br join(%v7, %j)

c8(%s: i64, %j: i64):
  %v8 = iadd.ovf %s, 23
  br join(%v8, %j)

c9(%s: i64, %j: i64):
  %v9 = iadd.ovf %s, 29
  br join(%v9, %j)

c10(%s: i64, %j: i64):
  %v10 = iadd.ovf %s, 31
  br join(%v10, %j)

c11(%s: i64, %j: i64):
  %v11 = iadd.ovf %s, 37
  br join(%v11, %j)

c12(%s: i64, %j: i64):
  %v12 = iadd.ovf %s, 41
  br join(%v12, %j)

c13(%s: i64, %j: i64):
  %v13 = iadd.ovf %s, 43
  br join(%v13, %j)

c14(%s: i64, %j: i64):
  %v14 = iadd.ovf %s, 47
  br join(%v14, %j)

c15(%s: i64, %j: i64):
  %v15 = iadd.ovf %s, 53
  br join(%v15, %j)

dflt(%s: i64, %j: i64):
  %vd = iadd.ovf %s, 1
  br join(%vd, %j)

join(%s1: i64, %j1: i64):
  %masked = and %s1, 268435455
  %next_i = iadd.ovf %j1, 1
  br loop(%masked, %next_i)

exit(%result: i64):
  %r = and %result, 255
  ret %r
}
