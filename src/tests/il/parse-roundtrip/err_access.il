il 0.3.0
func @f() -> void {
entry:
  eh.push ^H
  trap.from_err i32 7
normal:
  ret
handler ^H(%e:Error, %t:ResumeTok):
  eh.entry
  %kind = trap.kind
  %k = err.get_kind %e
  %ln = err.get_line %e
  resume.next %t
}
