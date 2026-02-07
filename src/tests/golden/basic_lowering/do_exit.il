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
extern @rt_trap(str) -> void
func @__mod_init$oop() -> void {
entry:
  ret
}
func @F() -> i64 {
entry_F:
  br L-1000000000_F
L-1000000000_F:
  .loc 1 2 1
  br do_head_0_F
L-999999999_F:
  .loc 1 5 8
  ret 0
ret_F:
  ret 0
do_head_0_F:
  .loc 1 2 1
  br do_body_0_F
do_body_0_F:
  .loc 1 3 1
  br do_end_0_F
do_end_0_F:
  .loc 1 2 1
  br L-999999999_F
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  ret 0
exit:
  ret 0
}
