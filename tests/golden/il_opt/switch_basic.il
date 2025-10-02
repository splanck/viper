il 0.1.2

func @switch_demo(i32 %value) -> i64 {
entry(%value: i32):
  switch.i32 %value, ^default, 1 -> ^one, 2 -> ^two
one:
  ret 1
two:
  ret 2
default:
  ret 42
}
