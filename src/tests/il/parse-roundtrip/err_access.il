il 0.1.2
func @f() -> void {
entry:
  eh.push ^H
  trap.from_err i32 7
  ret
handler ^H(%e:Error, %t:ResumeTok):
  eh.entry
  %kind = trap.kind
  %k = err.get_kind %e
  %ln = err.get_line %e
  resume.next %t
}
