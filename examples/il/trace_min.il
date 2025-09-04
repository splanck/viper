il 0.1

func @main() -> i64 {
entry:
  %t0 = add 1, 2
  %t1 = fadd 1.2345678901234567, 2.5
  %t2 = mul %t0, 3
  ret 0
}
