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
  %t16 = alloca 1
  .loc 1 6 21
  cbr %t15, and_rhs, and_false
L70:
  .loc 1 7 10
  %t27 = load i1, %t0
  .loc 1 7 4
  %t28 = zext1 %t27
  .loc 1 7 4
  %t29 = isub.ovf 0, %t28
  .loc 1 7 10
  call @Viper.Terminal.PrintI64(%t29)
  .loc 1 7 10
  %t30 = const_str @.L0
  .loc 1 7 10
  call @Viper.Terminal.PrintStr(%t30)
  .loc 1 7 4
  br exit
exit:
  ret 0
and_rhs:
  .loc 1 6 30
  %t17 = load i64, %t4
  .loc 1 6 32
  %t18 = icmp_ne %t17, 0
  .loc 1 6 32
  %t19 = zext1 %t18
  .loc 1 6 32
  %t20 = isub.ovf 0, %t19
  .loc 1 6 21
  %t21 = trunc1 %t20
  .loc 1 6 21
  store i1, %t16, %t21
  .loc 1 6 21
  br and_done
and_false:
  .loc 1 6 21
  %t22 = trunc1 0
  .loc 1 6 21
  store i1, %t16, %t22
  .loc 1 6 21
  br and_done
and_done:
  .loc 1 6 21
  %t23 = load i1, %t16
  .loc 1 6 21
  %t24 = zext1 %t23
  .loc 1 6 21
  %t25 = isub.ovf 0, %t24
  .loc 1 6 4
  %t26 = trunc1 %t25
  .loc 1 6 4
  store i1, %t0, %t26
  .loc 1 6 4
  br L70
}
