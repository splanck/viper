il 0.1.2

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
