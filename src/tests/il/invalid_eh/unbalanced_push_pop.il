il 0.1.3

func @unbalanced_push_pop() -> void {
entry:
  eh.push ^handler
  eh.push ^handler
  eh.pop
  ret

handler ^handler(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.next %tok
}
