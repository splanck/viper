il 0.1

func @main() -> i64 {
entry:
  br L3

L3:
  %t0 = add 1, 1
  ret %t0
}
