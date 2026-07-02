il 0.2.0

// Reduced-iteration variant of examples/il/benchmarks/checked_arith.il for the
// VM<->native differential and shared-corpus gates.

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 2000
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  // Operands bounded to [0, 65535] — no chain below can overflow i64.
  %x = and %i0, 65535
  %t1 = iadd.ovf %x, 1
  %t2 = imul.ovf %t1, 3
  %t3 = iadd.ovf %x, 41
  %t4 = iadd.ovf %t2, %t3
  %t5 = imul.ovf %t4, 5
  %t6 = isub.ovf %t5, %x
  %t7 = iadd.ovf %t6, 7
  %t8 = imul.ovf %t7, 3
  %t9 = isub.ovf %t8, 11
  %raw = iadd.ovf %sum0, %t9
  %new_sum = and %raw, 268435455
  %next_i = iadd.ovf %i0, 1
  br loop(%new_sum, %next_i)

exit(%result: i64):
  %r = and %result, 255
  ret %r
}
