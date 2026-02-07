il 0.2.0
extern @Viper.String.Asc(str) -> i64
extern @Viper.String.Chr(i64) -> str
extern @Viper.String.IndexOf(str, str) -> i64
extern @Viper.String.IndexOfFrom(str, i64, str) -> i64
extern @Viper.String.ToLower(str) -> str
extern @Viper.String.TrimStart(str) -> str
extern @Viper.String.Left(str, i64) -> str
extern @Viper.String.get_Length(str) -> i64
extern @Viper.String.Mid(str, i64) -> str
extern @Viper.String.MidLen(str, i64, i64) -> str
extern @Viper.String.TrimEnd(str) -> str
extern @Viper.String.Right(str, i64) -> str
extern @Viper.String.Cmp(str, str) -> i64
extern @Viper.String.CmpNoCase(str, str) -> i64
extern @Viper.String.Count(str, str) -> i64
extern @Viper.String.EndsWith(str, str) -> i1
extern @Viper.String.Flip(str) -> str
extern @Viper.String.Has(str, str) -> i1
extern @Viper.String.PadLeft(str, i64, str) -> str
extern @Viper.String.PadRight(str, i64, str) -> str
extern @Viper.String.Repeat(str, i64) -> str
extern @Viper.String.Replace(str, str, str) -> str
extern @Viper.String.Split(str, str) -> ptr
extern @Viper.String.StartsWith(str, str) -> i1
extern @Viper.String.Substring(str, i64, i64) -> str
extern @Viper.String.Trim(str) -> str
extern @Viper.String.ToUpper(str) -> str
extern @Viper.String.Join(str, ptr) -> str
extern @Viper.Terminal.PrintF64(f64) -> void
extern @Viper.Terminal.PrintI64(i64) -> void
extern @Viper.Terminal.PrintStr(str) -> void
extern @Viper.String.Length(str) -> i64
extern @rt_str_empty() -> str
global const str @.L0 = "ONE"
global const str @.L1 = "\n"
global const str @.L2 = "TWO"
global const str @.L3 = "OTHER"
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
exit:
  ret 0
if_test_0:
  .loc 1 2 7
  %t1 = load i64, %t0
  .loc 1 2 9
  %t2 = icmp_eq %t1, 1
  .loc 1 2 9
  %t3 = zext1 %t2
  .loc 1 2 9
  %t4 = isub.ovf 0, %t3
  .loc 1 2 4
  %t5 = trunc1 %t4
  .loc 1 2 9
  cbr %t5, if_then_0, if_test_1
if_then_0:
  .loc 1 2 24
  %t6 = const_str @.L0
  .loc 1 2 24
  call @Viper.Terminal.PrintStr(%t6)
  .loc 1 2 24
  %t7 = const_str @.L1
  .loc 1 2 24
  call @Viper.Terminal.PrintStr(%t7)
  .loc 1 2 4
  br if_exit
if_test_1:
  .loc 1 2 37
  %t8 = load i64, %t0
  .loc 1 2 39
  %t9 = icmp_eq %t8, 2
  .loc 1 2 39
  %t10 = zext1 %t9
  .loc 1 2 39
  %t11 = isub.ovf 0, %t10
  .loc 1 2 4
  %t12 = trunc1 %t11
  .loc 1 2 39
  cbr %t12, if_then_1, if_else
if_then_1:
  .loc 1 2 54
  %t13 = const_str @.L2
  .loc 1 2 54
  call @Viper.Terminal.PrintStr(%t13)
  .loc 1 2 54
  %t14 = const_str @.L1
  .loc 1 2 54
  call @Viper.Terminal.PrintStr(%t14)
  .loc 1 2 4
  br if_exit
if_else:
  .loc 1 2 71
  %t15 = const_str @.L3
  .loc 1 2 71
  call @Viper.Terminal.PrintStr(%t15)
  .loc 1 2 71
  %t16 = const_str @.L1
  .loc 1 2 71
  call @Viper.Terminal.PrintStr(%t16)
  .loc 1 2 4
  br if_exit
if_exit:
  .loc 1 2 4
  br exit
}
