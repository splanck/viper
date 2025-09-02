il 0.1
func @main(i1 %c) -> i64 {
entry:
  cbr %c, label L1, label L1
L1:
  ret 0
}
