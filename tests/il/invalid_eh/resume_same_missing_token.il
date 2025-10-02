il 0.1.2

func @resume_same_missing_token(%err:Error, %tok:ResumeTok) -> void {
entry(%err:Error, %tok:ResumeTok):
  br ^handler(%err, %tok)

handler ^handler(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.same %tok
}
