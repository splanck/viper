il 0.2.0
extern @Viper.Terminal.PrintStr(str) -> void
extern @Viper.Terminal.PrintI64(i64) -> void
extern @Viper.Terminal.PrintF64(f64) -> void
extern @Viper.String.get_Length(str) -> i64
extern @Viper.String.Substring(str, i64, i64) -> str
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
extern @Viper.String.Join(str, ptr) -> str
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
  call @Viper.Terminal.PrintI64(1)
  .loc 1 2 24
  %t7 = const_str @.L0
  .loc 1 2 24
  call @Viper.Terminal.PrintStr(%t7)
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
  call @Viper.Terminal.PrintI64(0)
  .loc 1 4 24
  %t18 = const_str @.L0
  .loc 1 4 24
  call @Viper.Terminal.PrintStr(%t18)
  .loc 1 4 4
  br if_exit1
if_else1:
  .loc 1 4 4
  br if_exit1
if_exit1:
  .loc 1 4 4
  br exit
}
