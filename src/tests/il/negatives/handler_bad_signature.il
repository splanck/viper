il 0.1.2
func @main() -> void {
entry:
  eh.push ^bad
  ret
handler ^bad(%err:Error, %token:ResumeTok):
  eh.entry
  resume.next %token
}
