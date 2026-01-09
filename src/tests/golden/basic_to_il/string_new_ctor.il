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
extern @rt_obj_new_i64(i64, i64) -> ptr
extern @rt_obj_retain_maybe(ptr) -> void
extern @rt_obj_release_check0(ptr) -> i1
extern @rt_obj_free(ptr) -> void
extern @Viper.Strings.FromStr(str) -> str
global const str @.L0 = "abc"
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
  br L20
L20:
  .loc 1 2 29
  %t1 = const_str @.L0
  .loc 1 2 29
  %t2 = call @Viper.Strings.FromStr(%t1)
  .loc 1 2 4
  %t3 = load ptr, %t0
  .loc 1 2 4
  %t4 = call @rt_obj_release_check0(%t3)
  .loc 1 2 4
  cbr %t4, obj_assign_dtor, obj_assign_cont
L30:
  .loc 1 3 4
  ret 0
exit:
  %t5 = load ptr, %t0
  %t6 = call @rt_obj_release_check0(%t5)
  cbr %t6, obj_epilogue_dtor, obj_epilogue_cont
obj_assign_dtor:
  .loc 1 2 4
  call @rt_obj_free(%t3)
  .loc 1 2 4
  br obj_assign_cont
obj_assign_cont:
  .loc 1 2 4
  call @rt_obj_retain_maybe(%t2)
  .loc 1 2 4
  store ptr, %t0, %t2
  .loc 1 2 4
  br L30
obj_epilogue_dtor:
  call @rt_obj_free(%t5)
  br obj_epilogue_cont
obj_epilogue_cont:
  store ptr, %t0, null
  ret 0
}
