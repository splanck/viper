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
extern @rt_mid2(str, i64) -> str
extern @rt_mid3(str, i64, i64) -> str
extern @rt_str_empty() -> str
extern @rt_str_retain_maybe(str) -> void
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "HELLO"
global const str @.L1 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  %t1 = call @rt_str_empty()
  store str, %t0, %t1
  br L10
L10:
  .loc 1 1 13
  %t2 = const_str @.L0
  .loc 1 1 4
  %t3 = load str, %t0
  .loc 1 1 4
  call @rt_str_release_maybe(%t3)
  .loc 1 1 4
  call @rt_str_retain_maybe(%t2)
  .loc 1 1 4
  store str, %t0, %t2
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 14
  %t4 = load str, %t0
  .loc 1 2 10
  %t5 = call @Viper.Strings.Len(%t4)
  .loc 1 2 10
  call @Viper.Console.PrintI64(%t5)
  .loc 1 2 10
  %t6 = const_str @.L1
  .loc 1 2 10
  call @Viper.Console.PrintStr(%t6)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 15
  %t7 = load str, %t0
  .loc 1 3 22
  %t8 = call @rt_mid3(%t7, 2, 3)
  .loc 1 3 22
  call @Viper.Console.PrintStr(%t8)
  .loc 1 3 22
  %t9 = const_str @.L1
  .loc 1 3 22
  call @Viper.Console.PrintStr(%t9)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 15
  %t10 = load str, %t0
  .loc 1 4 10
  %t11 = call @rt_mid2(%t10, 3)
  .loc 1 4 10
  call @Viper.Console.PrintStr(%t11)
  .loc 1 4 10
  %t12 = const_str @.L1
  .loc 1 4 10
  call @Viper.Console.PrintStr(%t12)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 4
  ret 0
exit:
  ret 0
}
