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
  %t2 = trunc1 -1
  .loc 1 2 4
  store i1, %t0, %t2
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  br if_test_0
L40:
  .loc 1 4 10
  %t5 = load i1, %t0
  .loc 1 4 4
  %t6 = zext1 %t5
  .loc 1 4 4
  %t7 = isub.ovf 0, %t6
  .loc 1 4 10
  call @Viper.Console.PrintI64(%t7)
  .loc 1 4 10
  %t8 = const_str @.L0
  .loc 1 4 10
  call @Viper.Console.PrintStr(%t8)
  .loc 1 4 4
  br exit
exit:
  ret 0
if_test_0:
  .loc 1 3 7
  %t3 = load i1, %t0
  .loc 1 3 7
  cbr %t3, if_then_0, if_else
if_then_0:
  .loc 1 3 20
  call @Viper.Console.PrintI64(1)
  .loc 1 3 20
  %t4 = const_str @.L0
  .loc 1 3 20
  call @Viper.Console.PrintStr(%t4)
  .loc 1 3 4
  br if_exit
if_else:
  .loc 1 3 4
  br if_exit
if_exit:
  .loc 1 3 4
  br L40
}
