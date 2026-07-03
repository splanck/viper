il 0.3.0

func @main() -> i64 {
entry:
  %is_negative = scmp_lt -7, 0
  cbr %is_negative, negative, positive

negative:
  %abs = isub.ovf 0, -7
  ret %abs

positive:
  ret -7
}
