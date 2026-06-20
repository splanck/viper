il 0.2.0

func @main() -> i64 {
entry:
  %take_left = icmp_eq 4, 4
  cbr %take_left, left, right

left:
  br join(22)

right:
  br join(11)

join(%value: i64):
  ret %value
}
