il 0.1.2
extern @Viper.Terminal.PrintStr(str) -> void
extern @Viper.Terminal.PrintI64(i64) -> void
extern @Viper.Terminal.PrintF64(f64) -> void
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
extern @rt_input_line() -> str
extern @Viper.Convert.ToInt(str) -> i64
extern @rt_str_empty() -> str
extern @Viper.Terminal.ReadLine() -> str
extern @rt_str_release_maybe(str) -> void
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
  %t2 = alloca 1
  %t3 = trunc1 0
  store i1, %t2, %t3
  %t4 = alloca 8
  %t5 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t6 = call @Viper.Terminal.ReadLine()
  .loc 1 1 4
  %t7 = call @Viper.Convert.ToInt(%t6)
  .loc 1 1 4
  store i64, %t5, %t7
  .loc 1 1 4
  call @rt_str_release_maybe(%t6)
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  %t8 = call @Viper.Terminal.ReadLine()
  .loc 1 2 4
  %t9 = call @Viper.Convert.ToInt(%t8)
  .loc 1 2 4
  store i64, %t4, %t9
  .loc 1 2 4
  call @rt_str_release_maybe(%t8)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 14
  %t10 = load i64, %t5
  .loc 1 5 16
  %t11 = icmp_ne %t10, 0
  .loc 1 5 16
  %t12 = zext1 %t11
  .loc 1 5 16
  %t13 = isub.ovf 0, %t12
  .loc 1 5 4
  %t14 = trunc1 %t13
  .loc 1 5 4
  store i1, %t2, %t14
  .loc 1 5 4
  br L60
L60:
  .loc 1 6 17
  %t15 = load i1, %t2
  .loc 1 6 21
  %t16 = zext1 %t15
  .loc 1 6 21
  %t17 = isub.ovf 0, %t16
  .loc 1 6 26
  %t18 = load i64, %t4
  .loc 1 6 28
  %t19 = icmp_ne %t18, 0
  .loc 1 6 28
  %t20 = zext1 %t19
  .loc 1 6 28
  %t21 = isub.ovf 0, %t20
  .loc 1 6 21
  %t22 = and %t17, %t21
  .loc 1 6 4
  %t23 = trunc1 %t22
  .loc 1 6 4
  store i1, %t0, %t23
  .loc 1 6 4
  br L70
L70:
  .loc 1 7 10
  %t24 = load i1, %t0
  .loc 1 7 4
  %t25 = zext1 %t24
  .loc 1 7 4
  %t26 = isub.ovf 0, %t25
  .loc 1 7 10
  call @Viper.Terminal.PrintI64(%t26)
  .loc 1 7 10
  %t27 = const_str @.L0
  .loc 1 7 10
  call @Viper.Terminal.PrintStr(%t27)
  .loc 1 7 4
  br exit
exit:
  ret 0
}
