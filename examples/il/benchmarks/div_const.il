il 0.2.0

// div_const: constant-division strength-reduction litmus.
// Signed and unsigned division/remainder by non-power-of-2 constants
// (3, 7, 10) plus a signed power of 2. Magic-multiply strength reduction
// replaces each hardware divide (~20-40 cycles) with a multiply+shift.
// Complements udiv_stress.il, which covers unsigned powers of 2 only.
// Expected exit-code checksum: 46

func @main() -> i64 {
entry:
  br loop(0, 1)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 20000001
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  %q1 = sdiv.chk0 %i0, 3
  %q2 = sdiv.chk0 %i0, 7
  %q3 = sdiv.chk0 %i0, 10
  %q4 = sdiv.chk0 %i0, 8
  %q5 = udiv.chk0 %i0, 7
  %q6 = udiv.chk0 %i0, 10
  %r1 = srem.chk0 %i0, 7
  %r2 = urem.chk0 %i0, 10

  %s1 = iadd.ovf %q1, %q2
  %s2 = iadd.ovf %s1, %q3
  %s3 = iadd.ovf %s2, %q4
  %s4 = iadd.ovf %s3, %q5
  %s5 = iadd.ovf %s4, %q6
  %s6 = iadd.ovf %s5, %r1
  %s7 = iadd.ovf %s6, %r2

  %raw = iadd.ovf %sum0, %s7
  %new_sum = and %raw, 268435455
  %next_i = iadd.ovf %i0, 1
  br loop(%new_sum, %next_i)

exit(%result: i64):
  %r = and %result, 255
  ret %r
}
