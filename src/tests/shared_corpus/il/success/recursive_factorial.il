il 0.3.0

func @fact(%n: i64) -> i64 {
entry(%n: i64):
  %base = scmp_le %n, 1
  cbr %base, done, recurse

done:
  ret 1

recurse:
  %next = isub.ovf %n, 1
  %sub = call @fact(%next)
  %product = imul.ovf %n, %sub
  ret %product
}

func @main() -> i64 {
entry:
  %value = call @fact(6)
  ret %value
}
