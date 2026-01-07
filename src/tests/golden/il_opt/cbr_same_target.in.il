il 0.1.3
func @main(i1 %c) -> i64 {
entry:
  cbr %c, L1, L1
L1:
  ret 0
}
