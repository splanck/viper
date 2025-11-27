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
  %t0 = alloca 8
  br L10
L10:
  .loc 1 1 4
  store i64, %t0, 2
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  br if_test_0
L30:
  .loc 1 3 4
  ret 0
exit:
  ret 0
if_test_0:
  .loc 1 2 7
  %t1 = load i64, %t0
  .loc 1 2 9
  %t2 = scmp_le %t1, 1
  .loc 1 2 9
  %t3 = zext1 %t2
  .loc 1 2 9
  %t4 = isub.ovf 0, %t3
  .loc 1 2 4
  %t5 = trunc1 %t4
  .loc 1 2 9
  cbr %t5, if_then_0, if_test_1
if_then_0:
  .loc 1 2 25
  call @Viper.Console.PrintI64(1)
  .loc 1 2 25
  %t6 = const_str @.L0
  .loc 1 2 25
  call @Viper.Console.PrintStr(%t6)
  .loc 1 2 4
  br if_exit
if_test_1:
  .loc 1 2 34
  %t7 = load i64, %t0
  .loc 1 2 36
  %t8 = scmp_le %t7, 2
  .loc 1 2 36
  %t9 = zext1 %t8
  .loc 1 2 36
  %t10 = isub.ovf 0, %t9
  .loc 1 2 4
  %t11 = trunc1 %t10
  .loc 1 2 36
  cbr %t11, if_then_1, if_else
if_then_1:
  .loc 1 2 52
  call @Viper.Console.PrintI64(2)
  .loc 1 2 52
  %t12 = const_str @.L0
  .loc 1 2 52
  call @Viper.Console.PrintStr(%t12)
  .loc 1 2 4
  br if_exit
if_else:
  .loc 1 2 65
  call @Viper.Console.PrintI64(3)
  .loc 1 2 65
  %t13 = const_str @.L0
  .loc 1 2 65
  call @Viper.Console.PrintStr(%t13)
  .loc 1 2 4
  br if_exit
if_exit:
  .loc 1 2 4
  br L30
}
