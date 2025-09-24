il 0.1 ; // File: examples/il/break_label.il // Purpose: Demonstrates branch to L3 and trap for breakpoint tests. // Links: docs/il-guide.md#reference#control-flow-terminators

func @main() -> i64 {
entry:
  br L3
L3:
  trap
}
