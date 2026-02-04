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
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 4
  eh.push ^handler_L100
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  eh.pop
  .loc 1 2 4
  eh.push ^handler_L200
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  eh.pop
  .loc 1 3 4
  eh.push ^handler_L300
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  eh.pop
  .loc 1 4 4
  ret 0
L100:
  .loc 1 5 5
  trap
L110:
  .loc 1 6 5
  eh.pop
  .loc 1 6 5
  ret 0
L200:
  .loc 1 7 5
  trap
L210:
  .loc 1 8 5
  eh.pop
  .loc 1 8 5
  ret 0
L300:
  .loc 1 9 5
  trap
L310:
  .loc 1 10 5
  eh.pop
  .loc 1 10 5
  ret 0
L400:
  .loc 1 11 5
  eh.pop
  .loc 1 11 5
  ret 0
exit:
  eh.pop
  ret 0
handler ^handler_L100(%err:Error, %tok:ResumeTok):
  eh.entry
  .loc 1 5 5
  eh.pop
  .loc 1 5 5
  ret 0
handler ^handler_L200(%err:Error, %tok:ResumeTok):
  eh.entry
  .loc 1 7 5
  eh.pop
  .loc 1 7 5
  ret 0
handler ^handler_L300(%err:Error, %tok:ResumeTok):
  eh.entry
  .loc 1 5 5
  resume.same %tok
}
