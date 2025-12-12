il 0.1.2
extern @Viper.Terminal.PrintStr(str) -> void
extern @Viper.Terminal.PrintI64(i64) -> void
extern @Viper.Terminal.PrintF64(f64) -> void
extern @Viper.Strings.Len(str) -> i64
extern @Viper.String.get_Length(str) -> i64
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
extern @rt_obj_retain_maybe(ptr) -> void
extern @rt_obj_release_check0(ptr) -> i1
extern @rt_obj_free(ptr) -> void
global const str @.L0 = "abcd"
global const str @.L1 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  br L10
L10:
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 12
  %t1 = const_str @.L0
  .loc 1 2 4
  %t2 = load ptr, %t0
  .loc 1 2 4
  %t3 = call @rt_obj_release_check0(%t2)
  .loc 1 2 4
  cbr %t3, obj_assign_dtor, obj_assign_cont
L30:
  .loc 1 3 10
  %t4 = load ptr, %t0
  .loc 1 3 10
  %t5 = load ptr, %t0
  .loc 1 3 12
  %t6 = call @Viper.Strings.Len(%t5)
  .loc 1 3 12
  call @Viper.Terminal.PrintI64(%t6)
  .loc 1 3 12
  %t7 = const_str @.L1
  .loc 1 3 12
  call @Viper.Terminal.PrintStr(%t7)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  ret 0
exit:
  %t8 = load ptr, %t0
  %t9 = call @rt_obj_release_check0(%t8)
  cbr %t9, obj_epilogue_dtor, obj_epilogue_cont
obj_assign_dtor:
  .loc 1 2 4
  call @rt_obj_free(%t2)
  .loc 1 2 4
  br obj_assign_cont
obj_assign_cont:
  .loc 1 2 4
  call @rt_obj_retain_maybe(%t1)
  .loc 1 2 4
  store ptr, %t0, %t1
  .loc 1 2 4
  br L30
obj_epilogue_dtor:
  call @rt_obj_free(%t8)
  br obj_epilogue_cont
obj_epilogue_cont:
  store ptr, %t0, null
  ret 0
}
