il 0.2.0
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
extern @rt_str_empty() -> str
extern @rt_trap(str) -> void
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @F() -> i64 {
entry_F:
  br L-1000000000_F
L-1000000000_F:
  .loc 1 2 1
  br if_test_0_F
ret_F:
  ret 0
if_test_0_F:
  .loc 1 2 1
  %t0 = trunc1 -1
  .loc 1 2 4
  cbr %t0, if_then_0_F, if_else_0_F
if_then_0_F:
  .loc 1 2 21
  ret 1
if_else_0_F:
  .loc 1 2 35
  ret 2
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br UL1000000000
UL1000000000:
  .loc 1 4 7
  %t0 = call @F()
  .loc 1 4 7
  call @Viper.Terminal.PrintI64(%t0)
  .loc 1 4 7
  %t1 = const_str @.L0
  .loc 1 4 7
  call @Viper.Terminal.PrintStr(%t1)
  .loc 1 4 1
  br exit
exit:
  ret 0
}
