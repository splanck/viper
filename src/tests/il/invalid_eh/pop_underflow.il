il 0.1.3

func @main() -> void {
entry:
  eh.push ^handler
  br ^first

first:
  eh.pop
  br ^second

second:
  eh.pop
  ret

handler ^handler(%err:Error, %tok:ResumeTok):
  eh.entry
  resume.next %tok
}
