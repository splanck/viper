il 0.3.0

// Redundant computation and constant propagation stress test.
// SCCP folds constant expressions to immediate values, reducing the number of
// dynamic operations per iteration.  Redundant subexpressions (%a1/%a2 and
// %b1/%b2 are identical) further tax the unoptimized path.

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 50000000
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  // Constant expressions: SCCP folds these to immediate constants.
  // %k1 = 30, %k2 = 90, %k3 = 50
  %k1 = iadd.ovf 10, 20
  %k2 = imul.ovf %k1, 3
  %k3 = isub.ovf %k2, 40

  // Redundant subexpressions: computed identically twice.
  %a1 = iadd.ovf %i0, 7
  %a2 = imul.ovf %a1, 3

  %b1 = iadd.ovf %i0, 7
  %b2 = imul.ovf %b1, 3

  // More constant folding chains.
  // %c1 = 300, %c2 = 600, %c3 = 500
  %c1 = iadd.ovf 100, 200
  %c2 = imul.ovf %c1, 2
  %c3 = isub.ovf %c2, 100

  // Third constant chain: %d1 = 15, %d2 = 75, %d3 = 70
  %d1 = iadd.ovf 5, 10
  %d2 = imul.ovf %d1, 5
  %d3 = isub.ovf %d2, 5

  // Live computation that uses the redundant pair and constants.
  // SCCP can fold %k3 -> 50, %c3 -> 500, %d3 -> 70.
  %live1 = iadd.ovf %a2, %b2
  %live2 = iadd.ovf %live1, %k3
  %live3 = iadd.ovf %live2, %c3
  %live4 = iadd.ovf %live3, %d3

  // Keep sum within range.
  %raw_sum = iadd.ovf %sum0, %live4
  %new_sum = and %raw_sum, 268435455

  %next_i  = iadd.ovf %i0, 1
  br loop(%new_sum, %next_i)

exit(%result: i64):
  ret %result
}
