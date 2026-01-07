il 0.1.2

func @add_triple(i64 %a, i64 %b, i64 %c) -> i64 {
entry(%a0: i64, %b0: i64, %c0: i64):
  %t1 = iadd.ovf %a0, %b0
  %t2 = iadd.ovf %t1, %c0
  ret %t2
}

func @mul_pair(i64 %x, i64 %y) -> i64 {
entry(%x0: i64, %y0: i64):
  %r = imul.ovf %x0, %y0
  ret %r
}

func @compute(i64 %n) -> i64 {
entry(%n0: i64):
  %a = iadd.ovf %n0, 0
  %b = iadd.ovf %n0, 1
  %c = iadd.ovf %n0, 2
  %sum = call @add_triple(%a, %b, %c)
  %result = call @mul_pair(%sum, 3)
  ret %result
}

func @main() -> i64 {
entry:
  %sum_slot = alloca 8
  %i_slot = alloca 8
  store i64, %sum_slot, 0
  store i64, %i_slot, 0
  br loop(0, 0)

loop(%sum: i64, %i: i64):
  %done = scmp_ge %i, 100000
  cbr %done, exit(%sum), body(%sum, %i)

body(%sum0: i64, %i0: i64):
  %r1 = call @compute(%i0)
  %r2 = call @add_triple(%i0, %r1, 1)
  %r3 = call @mul_pair(%r2, 2)
  %new_sum = iadd.ovf %sum0, %r3
  %next_i = iadd.ovf %i0, 1
  br loop(%new_sum, %next_i)

exit(%result: i64):
  ret %result
}
