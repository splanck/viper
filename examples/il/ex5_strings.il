il 0.1
extern @Viper.Console.PrintStr(str) -> void
extern @Viper.Console.PrintI64(i64) -> void
extern @Viper.Strings.Len(str) -> i64
extern @Viper.Strings.Concat(str, str) -> str
extern @Viper.Strings.Mid(str, i64, i64) -> str
extern @rt_str_eq(str, str) -> i1

global const str @.L0 = "JOHN"
global const str @.L1 = "DOE"
global const str @.L2 = " "
global const str @.L3 = "JOHN DOE"
global const str @.L4 = "\n"

func @main() -> i64 {
entry:
  %a_slot = alloca 8
  %b_slot = alloca 8
  %c_slot = alloca 8
  %l_slot = alloca 8

  %sA = const_str @.L0
  %sB = const_str @.L1
  store str, %a_slot, %sA
  store str, %b_slot, %sB

  %sSp = const_str @.L2
  %a0 = load str, %a_slot
  %c0 = call @Viper.Strings.Concat(%a0, %sSp)
  store str, %c_slot, %c0

  %c1 = load str, %c_slot
  %b0 = load str, %b_slot
  %c2 = call @Viper.Strings.Concat(%c1, %b0)
  store str, %c_slot, %c2

  %c3 = load str, %c_slot
  %out = const_str @.L3
  call @Viper.Console.PrintStr(%out)
  %nl0 = const_str @.L4
  call @Viper.Console.PrintStr(%nl0)

  %len = call @Viper.Strings.Len(%c3)
  store i64, %l_slot, %len
  call @Viper.Console.PrintI64(%len)
  %nl1 = const_str @.L4
  call @Viper.Console.PrintStr(%nl1)

  %first = call @Viper.Strings.Mid(%c3, 0, 1)
  call @Viper.Console.PrintStr(%first)
  %nl2 = const_str @.L4
  call @Viper.Console.PrintStr(%nl2)

  %target = const_str @.L3
  %eq = call @rt_str_eq(%c3, %target)
  cbr %eq, label then1, label else0

then1:
  call @Viper.Console.PrintI64(1)
  %nl3 = const_str @.L4
  call @Viper.Console.PrintStr(%nl3)
  br label exit

else0:
  call @Viper.Console.PrintI64(0)
  %nl4 = const_str @.L4
  call @Viper.Console.PrintStr(%nl4)
  br label exit

exit:
  ret 0
}
