il 0.1.2
func @f() -> void {
entry:
  eh.push ^H
  trap.kind Bounds
  ret
handler ^H(%e:Error, %t:ResumeTok):
  eh.entry
  %k = err.get_kind %e
  %ln = err.get_line %e
  resume.next %t
}
