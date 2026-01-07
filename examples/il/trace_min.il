il 0.1 ; // File: examples/il/trace_min.il // Purpose: Minimal program for demonstrating instruction tracing. // Links: docs/tools.md#ilc#run

func @main() -> i64 {
entry:
  %t0 = iadd.ovf 1, 2
  %t1 = fadd 1.2345678901234567, 2.5
  %t2 = fadd -0.0, 0.0
  %t3 = imul.ovf %t0, 3
  ret 0
}
