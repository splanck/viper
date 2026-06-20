il 0.2.0

func @main() -> i16 {
entry:
  %narrow:i16 = cast.si_narrow.chk 32768
  ret %narrow
}
