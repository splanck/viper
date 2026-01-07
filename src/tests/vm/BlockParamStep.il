il 0.1.2 ; // File: tests/vm/BlockParamStep.il // Purpose: Validate block parameter transfer while stepping. // Links: docs/il-guide.md#reference#control-flow-terminators

func @main() -> i64 {
entry:
  br callee(7)

callee(%x: i64):
  ret %x
}
