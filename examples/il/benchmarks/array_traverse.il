il 0.2.0

// array_traverse: addressing-mode and bounds-check litmus.
// Repeatedly walks a 1024-element i64 buffer with an idx.chk before every
// load. Exercises scaled-index address computation (shl+gep+load) and
// loop-invariant/provably-in-bounds check elimination.
// Expected exit-code checksum: 64

func @main() -> i64 {
entry:
  %buf = alloca 8192
  br init(0)

init(%i: i64):
  %filled = scmp_ge %i, 1024
  cbr %filled, passes(0, 0), fill(%i)

fill(%i0: i64):
  %off = shl %i0, 3
  %p = gep %buf, %off
  %v = imul.ovf %i0, 7
  store i64, %p, %v
  %next_i = iadd.ovf %i0, 1
  br init(%next_i)

passes(%pass: i64, %sum: i64):
  %done = scmp_ge %pass, 20000
  cbr %done, exit(%sum), inner(%pass, %sum, 0)

inner(%pass0: i64, %sum0: i64, %j: i64):
  %end = scmp_ge %j, 1024
  cbr %end, next_pass(%pass0, %sum0), body(%pass0, %sum0, %j)

body(%pass1: i64, %sum1: i64, %j0: i64):
  %jj = idx.chk %j0, 0, 1024
  %off2 = shl %jj, 3
  %p2 = gep %buf, %off2
  %v2 = load i64, %p2
  %s = iadd.ovf %sum1, %v2
  %s2 = and %s, 268435455
  %next_j = iadd.ovf %j0, 1
  br inner(%pass1, %s2, %next_j)

next_pass(%pass2: i64, %sum2: i64):
  %np = iadd.ovf %pass2, 1
  br passes(%np, %sum2)

exit(%result: i64):
  // Fold bits 8-15 into the checksum: each pass total is a multiple of 256,
  // so the raw low byte is structurally zero.
  %hi8 = lshr %result, 8
  %mix = xor %result, %hi8
  %r = and %mix, 255
  ret %r
}
