il 0.2.0

// Unsigned division stress test.
// Each udiv.chk0 by a power of 2 can be strength-reduced to a logical shift
// right by the codegen (ARM64: LSR, x86_64: SHR) at -O2.

func @main() -> i64 {
entry:
  br loop(0, 1)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 50000001
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  // Divide by ascending powers of 2.
  %d1  = udiv.chk0 %i0, 2
  %d2  = udiv.chk0 %i0, 4
  %d3  = udiv.chk0 %i0, 8
  %d4  = udiv.chk0 %i0, 16
  %d5  = udiv.chk0 %i0, 32
  %d6  = udiv.chk0 %i0, 64
  %d7  = udiv.chk0 %i0, 128
  %d8  = udiv.chk0 %i0, 256

  // Accumulate all quotients so nothing is dead.
  %s1 = iadd.ovf %d1, %d2
  %s2 = iadd.ovf %s1, %d3
  %s3 = iadd.ovf %s2, %d4
  %s4 = iadd.ovf %s3, %d5
  %s5 = iadd.ovf %s4, %d6
  %s6 = iadd.ovf %s5, %d7
  %s7 = iadd.ovf %s6, %d8

  // Keep sum within range by masking upper bits.
  %raw_sum = iadd.ovf %sum0, %s7
  %new_sum = and %raw_sum, 268435455

  %next_i  = iadd.ovf %i0, 1
  br loop(%new_sum, %next_i)

exit(%result: i64):
  ret %result
}
