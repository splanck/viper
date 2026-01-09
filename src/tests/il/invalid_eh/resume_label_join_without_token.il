il 0.2.0

func @resume_label_join_without_token(i1 %flag, Error %err, ResumeTok %tok) -> void {
entry:
  eh.push ^handler
  cbr %flag, handler(%err, %tok), trap_block

trap_block:
  trap

handler ^handler(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.label %tok, ^after(%err)

after(%forward_err:Error):
  ret
}
