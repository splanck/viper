il 0.3.0

func @main() -> i64 {
entry:
  %a = fadd 3.5, 2.25
  %b = cast.fp_to_si.rte.chk %a
  ret %b
}
