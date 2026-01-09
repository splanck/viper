il 0.2.0

func @resume_same_join_without_token(i1 %flag, Error %err, ResumeTok %tok) -> void {
entry:
  eh.push ^handler
  cbr %flag, handler(%err, %tok), trap_block

trap_block:
  trap

handler ^handler(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.same %tok
}
