// File: tests/il/e2e/rec_fact.il
// Purpose: Exercise VM recursion handling with a factorial function.
// Key invariants: Recursive calls allocate frames and produce correct result.
// Ownership/Lifetime: IL program executed by ilc for e2e testing.
// Links: docs/dev/vm.md

il 0.1.3
extern @rt_print_i64(i64) -> void

func @fact(i64 %n) -> i64 {
entry(%n0: i64):
  %c = scmp_le %n0, 1
  cbr %c, base, rec(%n0)
base:
  ret 1
rec(%n1: i64):
  %n2 = iadd.ovf %n1, -1
  %r = call @fact(%n2)
  %p = imul.ovf %n1, %r
  ret %p
}

func @main() -> i64 {
entry:
  %r = call @fact(10)
  call @rt_print_i64(%r)
  ret 0
}
