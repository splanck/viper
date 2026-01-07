il 0.1 ; // File: examples/il/watch_scalars.il // Purpose: Demonstrates watching scalar variables with ilc. // Links: docs/tools.md#ilc#watching-scalars

func @main() -> i64 {
entry:
  %x = alloca 8
  store i64, %x, 1
  store i64, %x, 1
  store i64, %x, 2
  ret 0
}
