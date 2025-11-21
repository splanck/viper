il 0.1 ; // File: examples/il/debug_script.il // Purpose: Uses a debugger script to step through execution. // Links: docs/tools.md#ilc#non-interactive-debugging-with--debug-cmds

extern @Viper.Console.PrintI64(i64) -> void
func @main() -> i64 {
entry:
  br L3
L3:
  %t0 = iadd.ovf 1, 2
  call @Viper.Console.PrintI64(%t0)
  %t1 = iadd.ovf %t0, 3
  call @Viper.Console.PrintI64(%t1)
  ret 0
}
