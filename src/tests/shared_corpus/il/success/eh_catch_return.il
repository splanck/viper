il 0.2.0

func @main() -> i64 {
entry:
  eh.push ^handler
  %value = sdiv.chk0 1, 0
  eh.pop
  ret %value

handler(%err: Error, %tok: ResumeTok):
  eh.entry
  %kind = trap.kind %err
  ret %kind
}
