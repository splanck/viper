il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
func @main() -> i64 {
entry:
  %t0 = alloca 8
  br label L10
L10:
  store i64, %t0, 2
  br label L20
L20:
  %t1 = load i64, %t0
  %t2 = scmp_le %t1, 1
  cbr %t2, label then, label else
L30:
  br label exit
exit:
  ret 0
then:
  call @rt_print_i64(1)
  br label exit1
exit1:
  br label L30
else:
  %t3 = load i64, %t0
  %t4 = scmp_le %t3, 2
  cbr %t4, label then1, label else1
then1:
  call @rt_print_i64(2)
  br label exit2
exit2:
  br label exit1
else1:
  call @rt_print_i64(3)
  br label exit2
}
