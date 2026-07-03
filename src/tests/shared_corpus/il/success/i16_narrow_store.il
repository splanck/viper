il 0.3.0

func @main() -> i16 {
entry:
  %slot = alloca 8
  %narrow:i16 = cast.si_narrow.chk 123
  store i16, %slot, %narrow
  %value = load i16, %slot
  ret %value
}
