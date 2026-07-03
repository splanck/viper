il 0.3.0

// Reduced-iteration variant of examples/il/benchmarks/select_diamond.il for the
// VM<->native differential and shared-corpus gates.

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 2000
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  // Scramble the counter so the comparisons flip unpredictably.
  %h0 = imul.ovf %i0, 2654435761
  %h1 = lshr %h0, 13
  %h2 = xor %h0, %h1
  %x = and %h2, 8191
  %y = and %h0, 8191
  // min(x, y) via a diamond
  %xlt = scmp_lt %x, %y
  cbr %xlt, minx(%sum0, %i0, %x), miny(%sum0, %i0, %y)

minx(%s0: i64, %j0: i64, %m0: i64):
  br clamp(%s0, %j0, %m0)

miny(%s1: i64, %j1: i64, %m1: i64):
  br clamp(%s1, %j1, %m1)

clamp(%s2: i64, %j2: i64, %m2: i64):
  // clamp the minimum to at most 4096 via a second diamond
  %big = scmp_gt %m2, 4096
  cbr %big, useb(%s2, %j2), usem(%s2, %j2, %m2)

useb(%s3: i64, %j3: i64):
  br join(%s3, %j3, 4096)

usem(%s4: i64, %j4: i64, %m4: i64):
  br join(%s4, %j4, %m4)

join(%s5: i64, %j5: i64, %v: i64):
  %acc = iadd.ovf %s5, %v
  %masked = and %acc, 268435455
  %next_i = iadd.ovf %j5, 1
  br loop(%masked, %next_i)

exit(%result: i64):
  %r = and %result, 255
  ret %r
}
