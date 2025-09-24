il 0.1 ; // File: examples/il/summary.il // Purpose: Exercises integer add and mul instructions. // Links: docs/il-guide.md#reference#integer-arithmetic

func @main() -> i64 {
entry:
  %t0 = add 1, 2
  %t1 = mul %t0, 3
  ret 0
}
