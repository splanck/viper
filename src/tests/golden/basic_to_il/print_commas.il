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
global const str @.L0 = "A"
global const str @.L1 = "             "
global const str @.L2 = "B"
global const str @.L3 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 10
  %t0 = const_str @.L0
  .loc 1 1 10
  call @Viper.Terminal.PrintStr(%t0)
  .loc 1 1 10
  %t1 = const_str @.L1
  .loc 1 1 10
  call @Viper.Terminal.PrintStr(%t1)
  .loc 1 1 15
  call @Viper.Terminal.PrintI64(1)
  .loc 1 1 15
  %t2 = const_str @.L1
  .loc 1 1 15
  call @Viper.Terminal.PrintStr(%t2)
  .loc 1 1 18
  %t3 = const_str @.L2
  .loc 1 1 18
  call @Viper.Terminal.PrintStr(%t3)
  .loc 1 1 18
  %t4 = const_str @.L3
  .loc 1 1 18
  call @Viper.Terminal.PrintStr(%t4)
  .loc 1 1 4
  br exit
exit:
  ret 0
}
