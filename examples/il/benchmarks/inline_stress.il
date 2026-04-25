il 0.2.0

// Inlining stress test.
// Small helper functions called in a tight loop.  At -O0 each call traverses
// the full interpreter dispatch; at -O2 with native codegen, the codegen can
// inline these tiny callees, eliminating call/return overhead.

// 1 instruction, 1 block: trivially inlinable by native codegen.
func @double(i64 %x) -> i64 {
entry(%x0: i64):
  %r = iadd.ovf %x0, %x0
  ret %r
}

// 1 instruction, 1 block: trivially inlinable by native codegen.
func @square(i64 %x) -> i64 {
entry(%x0: i64):
  %r = imul.ovf %x0, %x0
  ret %r
}

// 2 instructions, 1 block: trivially inlinable by native codegen.
func @add3(i64 %a, i64 %b, i64 %c) -> i64 {
entry(%a0: i64, %b0: i64, %c0: i64):
  %t = iadd.ovf %a0, %b0
  %r = iadd.ovf %t, %c0
  ret %r
}

// 1 instruction, 1 block: trivially inlinable by native codegen.
func @inc(i64 %x) -> i64 {
entry(%x0: i64):
  %r = iadd.ovf %x0, 1
  ret %r
}

// Chains the above helpers.  4 call instructions, 1 block.
func @combine(i64 %x) -> i64 {
entry(%x0: i64):
  %d  = call @double(%x0)
  %s  = call @square(%x0)
  %i  = call @inc(%x0)
  %r  = call @add3(%d, %s, %i)
  ret %r
}

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 50000000
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  // Each iteration makes 5 calls (1 to @combine -> 4 internal).
  // Native codegen at -O2 can inline these, while -O0 VM pays full
  // call dispatch overhead per call.
  %r = call @combine(%i0)

  // Keep sum within range.
  %raw_sum = iadd.ovf %sum0, %r
  %new_sum = and %raw_sum, 268435455

  %next_i  = iadd.ovf %i0, 1
  br loop(%new_sum, %next_i)

exit(%result: i64):
  ret %result
}
