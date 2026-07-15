il 0.3.0
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
extern @Viper.String.Compare(str, str) -> i64
extern @Viper.String.CompareIgnoreCase(str, str) -> i64
extern @Viper.String.Count(str, str) -> i64
extern @Viper.String.EndsWith(str, str) -> i1
extern @Viper.String.Reverse(str) -> str
extern @Viper.String.Contains(str, str) -> i1
extern @Viper.String.PadLeft(str, i64, str) -> str
extern @Viper.String.PadRight(str, i64, str) -> str
extern @Viper.String.Repeat(str, i64) -> str
extern @Viper.String.Replace(str, str, str) -> str
extern @Viper.String.Split(str, str) -> ptr
extern @Viper.String.Lines(str) -> ptr
extern @Viper.Text.Char.IsIdentifierStart(str) -> i1
extern @Viper.Text.Char.IsIdentifierPart(str) -> i1
extern @Viper.Text.Char.IsAlphanumeric(str) -> i1
extern @Viper.String.StartsWith(str, str) -> i1
extern @Viper.String.Substring(str, i64, i64) -> str
extern @Viper.String.Trim(str) -> str
extern @Viper.String.ToUpper(str) -> str
extern @Viper.String.Join(str, ptr) -> str
extern @Viper.Terminal.PrintF64(f64) -> void
extern @Viper.Terminal.PrintI64(i64) -> void
extern @Viper.Terminal.PrintStr(str) -> void
extern @rt_str_empty() -> str
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %a = alloca 1
  %t1 = trunc1 0
  store i1, %a, %t1
  br L10
L10:
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  %t2 = trunc1 -1
  .loc 1 2 4
  store i1, %a, %t2
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 10
  %t3 = load i1, %a
  .loc 1 3 12
  %t4 = zext1 %t3
  .loc 1 3 12
  %t5 = isub.ovf 0, %t4
  .loc 1 3 12
  %t6 = or %t5, 0
  .loc 1 3 12
  call @Viper.Terminal.PrintI64(%t6)
  .loc 1 3 12
  %t7 = const_str @.L0
  .loc 1 3 12
  call @Viper.Terminal.PrintStr(%t7)
  .loc 1 3 4
  br exit
exit:
  ret 0
}
