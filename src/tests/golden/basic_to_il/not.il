il 0.1.2
extern @Viper.Console.PrintStr(str) -> void
extern @Viper.Console.PrintI64(i64) -> void
extern @Viper.Console.PrintF64(f64) -> void
extern @Viper.Strings.Len(str) -> i64
extern @Viper.Strings.Mid(str, i64, i64) -> str
extern @Viper.String.Left(str, i64) -> str
extern @Viper.String.Right(str, i64) -> str
extern @Viper.String.Mid(str, i64) -> str
extern @Viper.String.MidLen(str, i64, i64) -> str
extern @Viper.String.Trim(str) -> str
extern @Viper.String.TrimStart(str) -> str
extern @Viper.String.TrimEnd(str) -> str
extern @Viper.String.ToUpper(str) -> str
extern @Viper.String.ToLower(str) -> str
extern @Viper.String.IndexOf(str, str) -> i64
extern @Viper.String.IndexOfFrom(i64, str, str) -> i64
extern @Viper.String.Chr(i64) -> str
extern @Viper.String.Asc(str) -> i64
extern @rt_str_empty() -> str
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 1
  %t1 = trunc1 0
  store i1, %t0, %t1
  br L10
L10:
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  br if_test_0
L30:
  .loc 1 3 16
  %t8 = load i1, %t0
  .loc 1 3 12
  %t9 = zext1 %t8
  .loc 1 3 12
  %t10 = isub.ovf 0, %t9
  .loc 1 3 12
  %t11 = xor %t10, -1
  .loc 1 3 4
  %t12 = trunc1 %t11
  .loc 1 3 4
  store i1, %t0, %t12
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  br if_test_01
exit:
  ret 0
if_test_0:
  .loc 1 2 11
  %t2 = load i1, %t0
  .loc 1 2 7
  %t3 = zext1 %t2
  .loc 1 2 7
  %t4 = isub.ovf 0, %t3
  .loc 1 2 7
  %t5 = xor %t4, -1
  .loc 1 2 4
  %t6 = trunc1 %t5
  .loc 1 2 7
  cbr %t6, if_then_0, if_else
if_then_0:
  .loc 1 2 24
  call @Viper.Console.PrintI64(1)
  .loc 1 2 24
  %t7 = const_str @.L0
  .loc 1 2 24
  call @Viper.Console.PrintStr(%t7)
  .loc 1 2 4
  br if_exit
if_else:
  .loc 1 2 4
  br if_exit
if_exit:
  .loc 1 2 4
  br L30
if_test_01:
  .loc 1 4 11
  %t13 = load i1, %t0
  .loc 1 4 7
  %t14 = zext1 %t13
  .loc 1 4 7
  %t15 = isub.ovf 0, %t14
  .loc 1 4 7
  %t16 = xor %t15, -1
  .loc 1 4 4
  %t17 = trunc1 %t16
  .loc 1 4 7
  cbr %t17, if_then_01, if_else1
if_then_01:
  .loc 1 4 24
  call @Viper.Console.PrintI64(0)
  .loc 1 4 24
  %t18 = const_str @.L0
  .loc 1 4 24
  call @Viper.Console.PrintStr(%t18)
  .loc 1 4 4
  br if_exit1
if_else1:
  .loc 1 4 4
  br if_exit1
if_exit1:
  .loc 1 4 4
  br exit
}
