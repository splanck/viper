il 0.3.0

func @main() -> i64 {
entry:
  %key:i32 = cast.si_narrow.chk 2
  switch.i32 %key, ^fallback, 1 -> ^one, 2 -> ^two

one:
  ret 10

two:
  ret 30

fallback:
  ret -1
}
