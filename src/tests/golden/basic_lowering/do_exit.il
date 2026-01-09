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
