il 0.3.0

extern @Viper.Terminal.PrintI64(i64) -> void

func @main() -> i64 {
entry:
  call @Viper.Terminal.PrintI64(123)
  ret 0
}
