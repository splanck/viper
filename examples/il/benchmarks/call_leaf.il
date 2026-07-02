il 0.2.0

// call_leaf: call-overhead and caller-saved-register litmus.
// A hot loop calling a small leaf function 20M times. The callee has six
// basic blocks so the O2 inliner (block budget 4) keeps the call, making
// this measure real call lowering: argument marshalling, caller-saved
// spill/rehoming around the call, and prologue/epilogue cost.
// Expected exit-code checksum: 206

func @step(%x: i64, %k: i64) -> i64 {
entry(%x: i64, %k: i64):
  %bit = and %k, 1
  %odd = icmp_ne %bit, 0
  cbr %odd, oddpath(%x, %k), evenpath(%x, %k)

oddpath(%x0: i64, %k0: i64):
  %t = imul.ovf %x0, 3
  %t1 = iadd.ovf %t, %k0
  br merge1(%t1, %k0)

evenpath(%x2: i64, %k2: i64):
  %t2 = shl %x2, 1
  %t3 = isub.ovf %t2, %k2
  br merge1(%t3, %k2)

merge1(%m: i64, %k3: i64):
  %bit2 = and %k3, 2
  %hi = icmp_ne %bit2, 0
  cbr %hi, highpath(%m), merge2(%m)

highpath(%m0: i64):
  %h = xor %m0, 12345
  br merge2(%h)

merge2(%m1: i64):
  %r = and %m1, 1048575
  ret %r
}

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 20000000
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  %r = call @step(%sum0, %i0)
  %s = iadd.ovf %sum0, %r
  %masked = and %s, 268435455
  %next_i = iadd.ovf %i0, 1
  br loop(%masked, %next_i)

exit(%result: i64):
  %rr = and %result, 255
  ret %rr
}
