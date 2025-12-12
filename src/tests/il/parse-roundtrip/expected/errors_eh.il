il 0.1.3
global const str @.msg0 = "next"
global const str @.msg1 = "after"

func @errors_demo() -> void {
entry:
  eh.push ^handle_same
  trap.from_err i32 6
  eh.push ^handle_next
  trap.from_err i32 4
  eh.push ^handle_label
  trap.from_err i32 5
  trap
  eh.pop
  ret
handler ^handle_same(%err:Error, %tok:ResumeTok):
  eh.entry
  %k0 = trap.kind
  resume.same %tok
handler ^handle_next(%err:Error, %tok:ResumeTok):
  eh.entry
  %msg0 = const_str @.msg0
  %err0 = trap.err 7, %msg0
  resume.next %tok
handler ^handle_label(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.label %tok, ^after(%err)
after(%incoming:Error):
  %msg1 = const_str @.msg1
  %err1 = trap.err 9, %msg1
  ret
}
