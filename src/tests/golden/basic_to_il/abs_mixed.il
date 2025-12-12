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
extern @rt_str_empty() -> str
extern @rt_abs_i64(i64) -> i64
extern @rt_abs_f64(f64) -> f64
global const str @.L0 = "\n"
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
  %t0 = call @rt_abs_i64(-1)
  .loc 1 1 10
  call @Viper.Terminal.PrintI64(%t0)
  .loc 1 1 10
  %t1 = const_str @.L0
  .loc 1 1 10
  call @Viper.Terminal.PrintStr(%t1)
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t2 = call @rt_abs_f64(-1.5)
  .loc 1 2 10
  call @Viper.Terminal.PrintF64(%t2)
  .loc 1 2 10
  %t3 = const_str @.L0
  .loc 1 2 10
  call @Viper.Terminal.PrintStr(%t3)
  .loc 1 2 4
  br exit
exit:
  ret 0
}
