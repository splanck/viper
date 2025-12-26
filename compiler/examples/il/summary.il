il 0.1 ; // File: examples/il/summary.il // Purpose: Exercises integer iadd.ovf and imul.ovf instructions. // Links: docs/il-guide.md#reference#integer-arithmetic

func @main() -> i64 {
entry:
  %t0 = iadd.ovf 1, 2
  %t1 = imul.ovf %t0, 3
  ret 0
}
