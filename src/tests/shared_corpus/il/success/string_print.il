il 0.2.0

extern @Viper.Terminal.PrintStr(str) -> void
global const str @.msg = "shared"

func @main() -> i64 {
entry:
  %msg = const_str @.msg
  call @Viper.Terminal.PrintStr(%msg)
  ret 0
}
