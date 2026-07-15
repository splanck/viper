il 0.3.0

extern @Viper.System.Environment.Exit(i64) -> void

func @main() -> i64 {
entry:
  call @Viper.System.Environment.Exit(7)
  ret 0
}
