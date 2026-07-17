il 0.3.0

extern @Zanna.System.Environment.Exit(i64) -> void

func @main() -> i64 {
entry:
  call @Zanna.System.Environment.Exit(7)
  ret 0
}
