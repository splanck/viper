il 0.3.0

func @resume_same_without_token(Error %err, ResumeTok %tok) -> void {
entry:
  br ^handler(%err, %tok)

handler ^handler(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.same %tok
}
