il 0.1.2

func @select(i64 %a, i64 %b, i1 %cond) -> i64 {
entry:
  cbr %cond, T(%a), F(%b)

T(%x: i64):
  ret %x

F(%y: i64):
  ret %y
}
