il 0.1.2 ; // File: examples/il/1.2/cbr_branch_args.il // Purpose: Demonstrates conditional branch with block arguments. // Links: docs/il-guide.md#reference#control-flow-terminators

func @select(i64 %a, i64 %b, i1 %cond) -> i64 {
entry:
  cbr %cond, T(%a), F(%b)

T(%x: i64):
  ret %x

F(%y: i64):
  ret %y
}
