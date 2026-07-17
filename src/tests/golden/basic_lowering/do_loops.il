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
extern @Zanna.String.StartsWith(str, str) -> i1
extern @Zanna.String.Substring(str, i64, i64) -> str
extern @Zanna.String.Trim(str) -> str
extern @Zanna.String.ToUpper(str) -> str
extern @Zanna.String.Join(str, ptr) -> str
extern @Zanna.Terminal.PrintF64(f64) -> void
extern @Zanna.Terminal.PrintI64(i64) -> void
extern @Zanna.Terminal.PrintStr(str) -> void
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
  .loc 1 4 1
  br do_body_1_F
L-999999998_F:
  .loc 1 6 8
  ret 0
ret_F:
  ret 0
do_head_0_F:
  .loc 1 2 1
  %t0 = trunc1 0
  .loc 1 2 10
  cbr %t0, do_body_0_F, do_end_0_F
do_body_0_F:
  .loc 1 2 10
  br do_head_0_F
do_end_0_F:
  .loc 1 2 1
  br L-999999999_F
do_head_1_F:
  .loc 1 4 1
  %t1 = trunc1 0
  .loc 1 5 12
  cbr %t1, do_end_1_F, do_body_1_F
do_body_1_F:
  .loc 1 4 1
  br do_head_1_F
do_end_1_F:
  .loc 1 4 1
  br L-999999998_F
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  ret 0
exit:
  ret 0
}
