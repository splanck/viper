il 0.1.2

func @resume_next_without_token(Error %err, ResumeTok %tok) -> void {
entry:
  br ^handler(%err, %tok)

handler ^handler(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.next %tok
}
