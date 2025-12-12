il 0.1.2
extern @Viper.Terminal.PrintStr(str) -> void
extern @Viper.Terminal.PrintI64(i64) -> void
extern @Viper.Terminal.PrintF64(f64) -> void
extern @Viper.Strings.Len(str) -> i64
extern @Viper.Strings.Mid(str, i64, i64) -> str
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
extern @Viper.String.IndexOfFrom(i64, str, str) -> i64
extern @Viper.String.Chr(i64) -> str
extern @Viper.String.Asc(str) -> i64
extern @rt_str_empty() -> str
extern @rt_obj_retain_maybe(ptr) -> void
extern @rt_obj_release_check0(ptr) -> i1
extern @rt_str_release_maybe(str) -> void
global const str @.L0 = "abcd"
global const str @.L1 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 11
  %t0 = const_str @.L0
  .loc 1 1 11
  %t1 = const_str @.L0
  .loc 1 1 19
  %t2 = call @Viper.Strings.Mid(%t1, 2, 2)
  .loc 1 1 19
  call @Viper.Terminal.PrintStr(%t2)
  .loc 1 1 19
  %t3 = const_str @.L1
  .loc 1 1 19
  call @Viper.Terminal.PrintStr(%t3)
  .loc 1 1 4
  call @rt_str_release_maybe(%t2)
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  ret 0
exit:
  ret 0
}
