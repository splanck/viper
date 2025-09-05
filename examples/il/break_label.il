// File: examples/il/break_label.il
// Purpose: Demonstrates a simple program for debugger breakpoints.
// Links: docs/debugging.md
il 0.1

func @main() -> i64 {
entry:
  br L3
L3:
  trap
  ret 0
}
