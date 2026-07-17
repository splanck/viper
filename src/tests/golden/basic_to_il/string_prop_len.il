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
extern @Zanna.String.Cmp(str, str) -> i64
extern @Zanna.String.CmpNoCase(str, str) -> i64
extern @Zanna.String.Count(str, str) -> i64
extern @Zanna.String.EndsWith(str, str) -> i1
extern @Zanna.String.Flip(str) -> str
extern @Zanna.String.Has(str, str) -> i1
extern @Zanna.String.PadLeft(str, i64, str) -> str
extern @Zanna.String.PadRight(str, i64, str) -> str
extern @Zanna.String.Repeat(str, i64) -> str
extern @Zanna.String.Replace(str, str, str) -> str
extern @Zanna.String.Split(str, str) -> ptr
extern @Zanna.String.Lines(str) -> ptr
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
  store ptr, %t0, null
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
  %t3:i1 = call @rt_obj_release_check0(%t2)
  .loc 1 2 4
  cbr %t3, obj_assign_dtor, obj_assign_cont
L30:
  .loc 1 3 10
  %t4 = load ptr, %t0
  .loc 1 3 10
  %t5 = load ptr, %t0
  .loc 1 3 12
  %t6:i64 = call @Zanna.String.get_Length(%t5)
  .loc 1 3 12
  call @Zanna.Terminal.PrintI64(%t6)
  .loc 1 3 12
  %t7 = const_str @.L1
  .loc 1 3 12
  call @Zanna.Terminal.PrintStr(%t7)
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  ret 0
exit:
  %t8 = load ptr, %t0
  %t9:i1 = call @rt_obj_release_check0(%t8)
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
