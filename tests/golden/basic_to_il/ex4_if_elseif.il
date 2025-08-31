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
  .loc 1 1 4
  store i64, %t0, 2
  .loc 1 1 4
  br label L20
L20:
  .loc 1 2 7
  %t1 = load i64, %t0
  .loc 1 2 9
  %t2 = scmp_le %t1, 1
  .loc 1 2 4
  cbr %t2, label then, label else
L30:
  .loc 1 3 4
  br label exit
exit:
  ret 0
then:
  .loc 1 2 19
  call @rt_print_i64(1)
  .loc 1 2 4
  br label exit1
exit1:
  .loc 1 2 4
  br label L30
else:
  .loc 1 2 34
  %t3 = load i64, %t0
  .loc 1 2 36
  %t4 = scmp_le %t3, 2
  .loc 1 2 27
  cbr %t4, label then1, label else1
then1:
  .loc 1 2 46
  call @rt_print_i64(2)
  .loc 1 2 27
  br label exit2
exit2:
  .loc 1 2 4
  br label exit1
else1:
  .loc 1 2 59
  call @rt_print_i64(3)
  .loc 1 2 27
  br label exit2
}
