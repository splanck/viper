il 0.1.3
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
extern @rt_open_err_vstr(str, i32, i32) -> i32
extern @rt_close_err(i32) -> i32
global const str @.L0 = "foo.txt"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 9
  %t0 = const_str @.L0
  .loc 1 1 4
  %t1:i32 = cast.si_narrow.chk 1
  .loc 1 1 4
  %t2:i32 = cast.si_narrow.chk 1
  .loc 1 1 4
  %t3 = call @rt_open_err_vstr(%t0, %t1, %t2)
  .loc 1 1 4
  %t4 = alloca 8
  .loc 1 1 4
  store i64, %t4, 0
  .loc 1 1 4
  store i32, %t4, %t3
  .loc 1 1 4
  %t5 = load i64, %t4
  .loc 1 1 4
  %t6 = icmp_ne %t5, 0
  .loc 1 1 4
  cbr %t6, open_fail, open_cont
L20:
  .loc 1 2 4
  %t7:i32 = cast.si_narrow.chk 1
  .loc 1 2 4
  %t8 = call @rt_close_err(%t7)
  .loc 1 2 4
  %t9 = alloca 8
  .loc 1 2 4
  store i64, %t9, 0
  .loc 1 2 4
  store i32, %t9, %t8
  .loc 1 2 4
  %t10 = load i64, %t9
  .loc 1 2 4
  %t11 = icmp_ne %t10, 0
  .loc 1 2 4
  cbr %t11, close_fail, close_cont
exit:
  ret 0
open_fail:
  .loc 1 1 4
  trap.from_err i32 %t3
open_cont:
  .loc 1 1 4
  br L20
close_fail:
  .loc 1 2 4
  trap.from_err i32 %t8
close_cont:
  .loc 1 2 4
  br exit
}
