il 0.1.2

func @main() -> i64 {
entry:
  br loop(0, 0)

loop(%count: i64, %i: i64):
  %done = scmp_ge %i, 200000
  cbr %done, exit(%count), check1(%count, %i)

check1(%c1: i64, %i1: i64):
  %mod2 = srem.chk0 %i1, 2
  %is_even = icmp_eq %mod2, 0
  cbr %is_even, even_path(%c1, %i1), check2(%c1, %i1)

even_path(%ce: i64, %ie: i64):
  %ce1 = iadd.ovf %ce, 1
  br check2(%ce1, %ie)

check2(%c2: i64, %i2: i64):
  %mod3 = srem.chk0 %i2, 3
  %div3 = icmp_eq %mod3, 0
  cbr %div3, div3_path(%c2, %i2), check3(%c2, %i2)

div3_path(%cd3: i64, %id3: i64):
  %cd31 = iadd.ovf %cd3, 2
  br check3(%cd31, %id3)

check3(%c3: i64, %i3: i64):
  %mod5 = srem.chk0 %i3, 5
  %div5 = icmp_eq %mod5, 0
  cbr %div5, div5_path(%c3, %i3), check4(%c3, %i3)

div5_path(%cd5: i64, %id5: i64):
  %cd51 = iadd.ovf %cd5, 3
  br check4(%cd51, %id5)

check4(%c4: i64, %i4: i64):
  %mod7 = srem.chk0 %i4, 7
  %div7 = icmp_eq %mod7, 0
  cbr %div7, div7_path(%c4, %i4), next(%c4, %i4)

div7_path(%cd7: i64, %id7: i64):
  %cd71 = iadd.ovf %cd7, 5
  br next(%cd71, %id7)

next(%cn: i64, %in: i64):
  %next_i = iadd.ovf %in, 1
  br loop(%cn, %next_i)

exit(%result: i64):
  ret %result
}
