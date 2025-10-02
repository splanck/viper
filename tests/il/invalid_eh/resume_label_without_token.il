il 0.1.2

func @resume_label_without_token(Error %err, ResumeTok %tok) -> void {
entry:
  br ^handler(%err, %tok)

handler ^handler(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.label %tok, ^after(%err)

after(%incoming:Error):
  ret
}
