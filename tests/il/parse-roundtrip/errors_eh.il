il 0.1.2
func @errors_demo() -> void {
entry:
  eh.push ^handle_same
  trap.kind DivideByZero
  eh.push ^handle_next
  trap.kind Overflow
  eh.push ^handle_label
  trap.kind InvalidCast
  trap.kind DomainError
  eh.pop
  ret
handler ^handle_same(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.same %tok
handler ^handle_next(%err:Error, %tok:ResumeTok):
  eh.entry
  trap.err %err
  resume.next %tok
handler ^handle_label(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.label %tok, ^after(%err)
after(%incoming:Error):
  trap.err %incoming
  ret
}
