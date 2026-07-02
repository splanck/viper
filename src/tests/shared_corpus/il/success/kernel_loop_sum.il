il 0.2.0

// Reduced-iteration variant of examples/il/benchmarks/loop_sum.il for the
// VM<->native differential and shared-corpus gates.

func @main() -> i64 {
entry:
  br loop(0, 0, 0, 0)

loop(%a: i64, %b: i64, %c: i64, %i: i64):
  %done = scmp_ge %i, 2000
  cbr %done, exit(%a, %b, %c), body(%a, %b, %c, %i)

body(%a0: i64, %b0: i64, %c0: i64, %i0: i64):
  %a1 = iadd.ovf %a0, %i0
  %b1 = xor %b0, %a1
  %c1 = iadd.ovf %c0, %b1
  %c2 = and %c1, 1048575
  %next_i = iadd.ovf %i0, 1
  br loop(%a1, %b1, %c2, %next_i)

exit(%ra: i64, %rb: i64, %rc: i64):
  %t0 = xor %ra, %rb
  %t1 = iadd.ovf %t0, %rc
  %r = and %t1, 255
  ret %r
}
