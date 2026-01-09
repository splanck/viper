il 0.2.0
func @main(i1 %c) -> i64 {
entry:
  cbr %c, L1, L1
L1:
  ret 0
}
