il 0.1

func @main() -> i64 {
entry:
  %t0 = add 1, 2
  %t1 = mul %t0, 3
  ret 0
}
