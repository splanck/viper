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
extern @rt_str_mid(str, i64) -> str
extern @rt_str_mid_len(str, i64, i64) -> str
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
  %t5 = call @Viper.String.get_Length(%t4)
  .loc 1 2 10
  call @Viper.Terminal.PrintI64(%t5)
  .loc 1 2 10
  %t6 = const_str @.L1
  .loc 1 2 10
  call @Viper.Terminal.PrintStr(%t6)
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 15
  %t7 = load str, %t0
  .loc 1 3 22
  %t8 = call @rt_str_mid_len(%t7, 2, 3)
  .loc 1 3 22
  call @Viper.Terminal.PrintStr(%t8)
  .loc 1 3 22
  %t9 = const_str @.L1
  .loc 1 3 22
  call @Viper.Terminal.PrintStr(%t9)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 15
  %t10 = load str, %t0
  .loc 1 4 10
  %t11 = call @rt_str_mid(%t10, 3)
  .loc 1 4 10
  call @Viper.Terminal.PrintStr(%t11)
  .loc 1 4 10
  %t12 = const_str @.L1
  .loc 1 4 10
  call @Viper.Terminal.PrintStr(%t12)
  .loc 1 4 4
  br L50
L50:
  .loc 1 5 4
  ret 0
exit:
  ret 0
}
