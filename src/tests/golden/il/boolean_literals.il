il 0.1.3
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
extern @Viper.String.IndexOfFrom(str, i64, str) -> i64
extern @Viper.String.Chr(i64) -> str
extern @Viper.String.Asc(str) -> i64
extern @Viper.String.Replace(str, str, str) -> str
extern @Viper.String.StartsWith(str, str) -> i1
extern @Viper.String.EndsWith(str, str) -> i1
extern @Viper.String.Has(str, str) -> i1
extern @Viper.String.Count(str, str) -> i64
extern @Viper.String.PadLeft(str, i64, str) -> str
extern @Viper.String.PadRight(str, i64, str) -> str
extern @Viper.String.Split(str, str) -> ptr
extern @Viper.String.Repeat(str, i64) -> str
extern @Viper.String.Flip(str) -> str
extern @Viper.String.Cmp(str, str) -> i64
extern @Viper.String.CmpNoCase(str, str) -> i64
extern @Viper.Strings.Join(str, ptr) -> str
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
  %t2 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t3 = call @Viper.Terminal.ReadLine()
  .loc 1 1 4
  %t4 = call @Viper.Convert.ToInt(%t3)
  .loc 1 1 4
  store i64, %t2, %t4
  .loc 1 1 4
  call @rt_str_release_maybe(%t3)
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 12
  %t5 = load i64, %t2
  .loc 1 3 14
  %t6 = icmp_ne %t5, 0
  .loc 1 3 14
  %t7 = zext1 %t6
  .loc 1 3 14
  %t8 = isub.ovf 0, %t7
  .loc 1 3 4
  %t9 = trunc1 %t8
  .loc 1 3 4
  store i1, %t0, %t9
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 10
  %t10 = load i1, %t0
  .loc 1 4 12
  %t11 = zext1 %t10
  .loc 1 4 12
  %t12 = isub.ovf 0, %t11
  .loc 1 4 12
  %t13 = or %t12, -1
  .loc 1 4 12
  call @Viper.Terminal.PrintI64(%t13)
  .loc 1 4 12
  %t14 = const_str @.L0
  .loc 1 4 12
  call @Viper.Terminal.PrintStr(%t14)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 20
  %t15 = load i1, %t0
  .loc 1 5 16
  %t16 = zext1 %t15
  .loc 1 5 16
  %t17 = isub.ovf 0, %t16
  .loc 1 5 16
  %t18 = and 0, %t17
  .loc 1 5 16
  call @Viper.Terminal.PrintI64(%t18)
  .loc 1 5 16
  %t19 = const_str @.L0
  .loc 1 5 16
  call @Viper.Terminal.PrintStr(%t19)
  .loc 1 5 4
  br exit
exit:
  ret 0
}
