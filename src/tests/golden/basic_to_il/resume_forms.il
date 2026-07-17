il 0.3.0
extern @Zanna.Terminal.PrintStr(str) -> void
extern @Zanna.Terminal.PrintI64(i64) -> void
extern @Zanna.Terminal.PrintF64(f64) -> void
extern @Zanna.String.get_Length(str) -> i64
extern @Zanna.String.Substring(str, i64, i64) -> str
extern @Zanna.String.Left(str, i64) -> str
extern @Zanna.String.Right(str, i64) -> str
extern @Zanna.String.Mid(str, i64) -> str
extern @Zanna.String.MidLen(str, i64, i64) -> str
extern @Zanna.String.Trim(str) -> str
extern @Zanna.String.TrimStart(str) -> str
extern @Zanna.String.TrimEnd(str) -> str
extern @Zanna.String.ToUpper(str) -> str
extern @Zanna.String.ToLower(str) -> str
extern @Zanna.String.IndexOf(str, str) -> i64
extern @Zanna.String.IndexOfFrom(str, i64, str) -> i64
extern @Zanna.String.Chr(i64) -> str
extern @Zanna.String.Asc(str) -> i64
extern @Zanna.String.Replace(str, str, str) -> str
extern @Zanna.String.StartsWith(str, str) -> i1
extern @Zanna.String.EndsWith(str, str) -> i1
extern @Zanna.String.Has(str, str) -> i1
extern @Zanna.String.Count(str, str) -> i64
extern @Zanna.String.PadLeft(str, i64, str) -> str
extern @Zanna.String.PadRight(str, i64, str) -> str
extern @Zanna.String.Split(str, str) -> ptr
extern @Zanna.String.Lines(str) -> ptr
extern @Zanna.String.Repeat(str, i64) -> str
extern @Zanna.String.Flip(str) -> str
extern @Zanna.String.Cmp(str, str) -> i64
extern @Zanna.String.CmpNoCase(str, str) -> i64
extern @Zanna.String.Join(str, ptr) -> str
extern @rt_str_empty() -> str
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  br L10
L10:
  .loc 1 1 4
  eh.push ^handler_L100
  .loc 1 1 4
  br L20
L20:
  .loc 1 2 4
  eh.pop
  .loc 1 2 4
  eh.push ^handler_L200
  .loc 1 2 4
  br L30
L30:
  .loc 1 3 4
  eh.pop
  .loc 1 3 4
  eh.push ^handler_L300
  .loc 1 3 4
  br L40
L40:
  .loc 1 4 4
  eh.pop
  .loc 1 4 4
  ret 0
L100:
  .loc 1 5 5
  trap
L110:
  .loc 1 6 5
  eh.pop
  .loc 1 6 5
  ret 0
L200:
  .loc 1 7 5
  trap
L210:
  .loc 1 8 5
  eh.pop
  .loc 1 8 5
  ret 0
L300:
  .loc 1 9 5
  trap
L310:
  .loc 1 10 5
  eh.pop
  .loc 1 10 5
  ret 0
L400:
  .loc 1 11 5
  eh.pop
  .loc 1 11 5
  ret 0
exit:
  eh.pop
  ret 0
handler ^handler_L100(%t0:Error, %t1:ResumeTok):
  eh.entry
  .loc 1 5 5
  eh.pop
  .loc 1 5 5
  ret 0
handler ^handler_L200(%t2:Error, %t3:ResumeTok):
  eh.entry
  .loc 1 7 5
  eh.pop
  .loc 1 7 5
  ret 0
handler ^handler_L300(%t4:Error, %t5:ResumeTok):
  eh.entry
  .loc 1 5 5
  resume.same %t5
}
