il 0.1.2 ; // File: examples/il/1.2/loop_block_params.il // Purpose: Illustrates a loop using block parameters to accumulate a sum. // Links: docs/il-guide.md#reference#functions--basic-blocks

func @sum_to_three() -> i64 {
entry:
  br loop(0, 0)

loop(%i: i64, %acc: i64):
  %cond = scmp_ge %i, 3
  cbr %cond, exit(%acc), body(%i, %acc)

body(%i: i64, %acc: i64):
  %acc1 = iadd.ovf %acc, %i
  %i1 = iadd.ovf %i, 1
  br loop(%i1, %acc1)

exit(%result: i64):
  ret %result
}
