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
  store i64, %t1, 1
  br label for_head
exit:
  ret 0
for_head:
  %t2 = load i64, %t1
  %t3 = scmp_le %t2, 5
  cbr %t3, label for_body, label for_done
for_body:
  store i64, %t0, 1
  br label for_head1
for_inc:
  %t11 = load i64, %t1
  %t12 = add %t11, 1
  store i64, %t1, %t12
  br label for_head
for_done:
  br label exit
for_head1:
  %t4 = load i64, %t0
  %t5 = scmp_le %t4, 5
  cbr %t5, label for_body1, label for_done1
for_body1:
  %t6 = load i64, %t1
  %t7 = load i64, %t0
  %t8 = mul %t6, %t7
  call @rt_print_i64(%t8)
  br label for_inc1
for_inc1:
  %t9 = load i64, %t0
  %t10 = add %t9, 1
  store i64, %t0, %t10
  br label for_head1
for_done1:
  br label for_inc
}
