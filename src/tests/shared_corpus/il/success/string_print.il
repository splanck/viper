il 0.3.0

extern @Zanna.Terminal.PrintStr(str) -> void
global const str @.msg = "shared"

func @main() -> i64 {
entry:
  %msg = const_str @.msg
  call @Zanna.Terminal.PrintStr(%msg)
  ret 0
}
