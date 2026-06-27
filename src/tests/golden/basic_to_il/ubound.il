il VERSION
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
extern @Viper.String.Lines(str) -> ptr
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
extern @rt_arr_i64_new(i64) -> ptr
extern @rt_arr_i64_len(ptr) -> i64
extern @rt_arr_i64_retain(ptr) -> void
extern @rt_arr_i64_release(ptr) -> void
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %a = alloca 8
  store ptr, %a, null
  %t1 = alloca 8
  br L10
L10:
  .loc 1 1 4
  %t2:ptr = call @rt_arr_i64_new(1)
  .loc 1 1 4
  call @rt_arr_i64_retain(%t2)
  .loc 1 1 4
  %t3 = load ptr, %a
  .loc 1 1 4
  call @rt_arr_i64_release(%t3)
  .loc 1 1 4
  store ptr, %a, %t2
  .loc 1 1 4
  store i64, %t1, 1
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  call @Viper.Terminal.PrintI64(0)
  .loc 1 2 10
  %t4 = const_str @.L0
  .loc 1 2 10
  call @Viper.Terminal.PrintStr(%t4)
  .loc 1 2 4
  br exit
exit:
  %t5 = load ptr, %a
  call @rt_arr_i64_release(%t5)
  store ptr, %a, null
  ret 0
}
