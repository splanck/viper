il 0.1
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
func @main() -> i64 {
entry:
  %t0 = alloca 8
  %t1 = alloca 8
  br label L10
L10:
  store i64, %t1, 0
  br label L20
L20:
  store i64, %t0, 1
  br label L30
L30:
L70:
  %t9 = load i64, %t1
  call @rt_print_i64(%t9)
  br label L80
L80:
  br label exit
exit:
  ret 0
loop_head:
  %t2 = load i64, %t0
  %t3 = scmp_le %t2, 10
  cbr %t3, label loop_body, label done
loop_body:
  %t4 = load i64, %t1
  %t5 = load i64, %t0
  %t6 = add %t4, %t5
  store i64, %t1, %t6
  %t7 = load i64, %t0
  %t8 = add %t7, 1
  store i64, %t0, %t8
  br label loop_head
done:
  br label L70
}
