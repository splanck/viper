il 0.2.0

func @resume_label_not_postdom(i1 %flag, Error %err, ResumeTok %tok) -> void {
entry:
  eh.push ^handler
  cbr %flag, ^left, ^right

left:
  eh.pop
  br exit

right:
  %tmp:i64 = sdiv.chk0 1, 1
  eh.pop
  br exit

exit:
  ret

handler ^handler(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.label %tok, ^left
}
