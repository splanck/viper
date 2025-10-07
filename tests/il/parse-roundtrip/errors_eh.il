il 0.1.2
global const str @.msg0 = "next"
global const str @.msg1 = "after"

func @errors_demo() -> void {
entry:
  eh.push ^handle_same
  trap.from_err i32 6
after_same:
  eh.push ^handle_next
  trap.from_err i32 4
after_next:
  eh.push ^handle_label
  trap.from_err i32 5
after_label:
  trap
handler ^handle_same(%err:Error, %tok:ResumeTok):
  eh.entry
  %k0 = trap.kind
  resume.label %tok, ^after_same
handler ^handle_next(%err:Error, %tok:ResumeTok):
  eh.entry
  %msg0 = const_str @.msg0
  %err0 = trap.err 7, %msg0
  resume.label %tok, ^after_next
handler ^handle_label(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.label %tok, ^after(%err)
after(%incoming:Error):
  eh.pop
  %msg1 = const_str @.msg1
  %err1 = trap.err 9, %msg1
  ret
}
