il 0.3.0

func @main() -> i64 {
entry:
  %a = cast.fp_to_si.rte.chk 2.5
  %b = cast.fp_to_si.rte.chk 3.5
  %sum = iadd.ovf %a, %b
  ret %sum
}
