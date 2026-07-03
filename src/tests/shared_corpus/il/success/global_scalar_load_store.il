il 0.3.0

global i64 @counter = 41

func @main() -> i64 {
entry:
  %ptr = gaddr @counter
  store i64, %ptr, 58
  %value = load i64, %ptr
  ret %value
}
