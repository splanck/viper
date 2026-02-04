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
  %t0 = alloca 8
  store ptr, %t0, null
  br L10
L10:
  .loc 1 1 4
  %t1 = call @rt_arr_i64_new(1)
  .loc 1 1 4
  call @rt_arr_i64_retain(%t1)
  .loc 1 1 4
  %t2 = load ptr, %t0
  .loc 1 1 4
  call @rt_arr_i64_release(%t2)
  .loc 1 1 4
  store ptr, %t0, %t1
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 10
  %t3 = load ptr, %t0
  .loc 1 2 10
  %t4 = call @rt_arr_i64_len(%t3)
  .loc 1 2 10
  %t5 = isub.ovf %t4, 1
  .loc 1 2 10
  call @Viper.Terminal.PrintI64(%t5)
  .loc 1 2 10
  %t6 = const_str @.L0
  .loc 1 2 10
  call @Viper.Terminal.PrintStr(%t6)
  .loc 1 2 4
  br exit
exit:
  %t7 = load ptr, %t0
  call @rt_arr_i64_release(%t7)
  store ptr, %t0, null
  ret 0
}
