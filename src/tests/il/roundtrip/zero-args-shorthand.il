il 0.2.0

func @main() -> i64 {
entry:
  cbr 1, then, else

then:
  br end

else:
  br end

end:
  ret 0
}
