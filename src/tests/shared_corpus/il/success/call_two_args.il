il 0.2.0

func @mix(%a: i64, %b: i64) -> i64 {
entry(%a: i64, %b: i64):
  %sum = iadd.ovf %a, %b
  %value = imul.ovf %sum, 3
  ret %value
}

func @main() -> i64 {
entry:
  %result = call @mix(14, 27)
  ret %result
}
