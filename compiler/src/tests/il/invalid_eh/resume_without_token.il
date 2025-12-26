il 0.1.3

func @resume_without_token(Error %err, ResumeTok %tok) -> void {
entry:
  br ^handler(%err, %tok)

handler ^handler(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.same %tok
}
