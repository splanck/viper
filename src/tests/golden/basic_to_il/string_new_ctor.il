il 0.3.0
extern @Zanna.String.Asc(str) -> i64
extern @Zanna.String.Chr(i64) -> str
extern @Zanna.String.IndexOf(str, str) -> i64
extern @Zanna.String.IndexOfFrom(str, i64, str) -> i64
extern @Zanna.String.ToLower(str) -> str
extern @Zanna.String.TrimStart(str) -> str
extern @Zanna.String.Left(str, i64) -> str
extern @Zanna.String.get_Length(str) -> i64
extern @Zanna.String.Mid(str, i64) -> str
extern @Zanna.String.MidLen(str, i64, i64) -> str
extern @Zanna.String.TrimEnd(str) -> str
extern @Zanna.String.Right(str, i64) -> str
extern @Zanna.String.Compare(str, str) -> i64
extern @Zanna.String.CompareIgnoreCase(str, str) -> i64
extern @Zanna.String.Count(str, str) -> i64
extern @Zanna.String.EndsWith(str, str) -> i1
extern @Zanna.String.Reverse(str) -> str
extern @Zanna.String.Contains(str, str) -> i1
extern @Zanna.String.PadLeft(str, i64, str) -> str
extern @Zanna.String.PadRight(str, i64, str) -> str
extern @Zanna.String.Repeat(str, i64) -> str
extern @Zanna.String.Replace(str, str, str) -> str
extern @Zanna.String.Split(str, str) -> ptr
extern @Zanna.String.Lines(str) -> ptr
extern @Zanna.Text.Char.IsIdentifierStart(str) -> i1
extern @Zanna.Text.Char.IsIdentifierPart(str) -> i1
extern @Zanna.Text.Char.IsAlphanumeric(str) -> i1
extern @Zanna.String.StartsWith(str, str) -> i1
extern @Zanna.String.Substring(str, i64, i64) -> str
extern @Zanna.String.Trim(str) -> str
extern @Zanna.String.ToUpper(str) -> str
extern @Zanna.String.Join(str, ptr) -> str
extern @Zanna.Terminal.PrintF64(f64) -> void
extern @Zanna.Terminal.PrintI64(i64) -> void
extern @Zanna.Terminal.PrintStr(str) -> void
extern @rt_str_empty() -> str
extern @rt_obj_retain_maybe(ptr) -> void
extern @rt_obj_release_check0(ptr) -> i1
extern @rt_obj_free(ptr) -> void
extern @rt_str_retain_maybe(str) -> void
extern @rt_str_release_maybe(str) -> void
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %s = alloca 8
  %t1:str = call @rt_str_empty()
  store str, %s, %t1
  br L10
L10:
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 12
  %t2:str = call @Zanna.String.Chr(65)
  .loc 1 2 4
  call @rt_str_retain_maybe(%t2)
  .loc 1 2 4
  %t3 = load str, %s
  .loc 1 2 4
  call @rt_str_release_maybe(%t3)
  .loc 1 2 4
  store str, %s, %t2
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  ret 0
exit:
  %t4 = load ptr, %s
  %t5:i1 = call @rt_obj_release_check0(%t4)
  cbr %t5, obj_epilogue_dtor, obj_epilogue_cont
obj_epilogue_dtor:
  call @rt_obj_free(%t4)
  br obj_epilogue_cont
obj_epilogue_cont:
  store ptr, %s, null
  ret 0
}
