il 0.2.0

extern @Viper.System.Environment.EndProgram(i64) -> void

func @main() -> i64 {
entry:
  call @Viper.System.Environment.EndProgram(7)
  ret 0
}
