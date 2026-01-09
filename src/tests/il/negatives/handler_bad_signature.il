il 0.2.0
func @main() -> void {
entry:
  eh.push ^bad
  ret
handler ^bad(%err:Error, %token:ResumeTok):
  eh.entry
  resume.next %token
}
