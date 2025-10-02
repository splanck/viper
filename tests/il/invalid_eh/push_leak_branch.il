il 0.1.2

func @main(i1 %flag) -> void {
entry:
  eh.push ^handler
  cbr %flag, leak, safe

safe:
  eh.pop
  ret

leak:
  ret

handler ^handler(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.next %tok
}
