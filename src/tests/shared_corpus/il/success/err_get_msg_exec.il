il 0.3.0

func @main() -> i64 {
entry:
  eh.push ^handler
  %value = sdiv.chk0 1, 0
  eh.pop
  ret %value

handler(%err: Error, %tok: ResumeTok):
  eh.entry
  %msg = err.get_msg %err
  ret 7
}
