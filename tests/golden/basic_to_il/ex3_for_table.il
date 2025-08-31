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
  .loc 1 1 4
  store i64, %t1, 1
  .loc 1 1 4
  br label for_head
exit:
  ret 0
for_head:
  .loc 1 1 4
  %t2 = load i64, %t1
  .loc 1 1 4
  %t3 = scmp_le %t2, 5
  .loc 1 1 4
  cbr %t3, label for_body, label for_done
for_body:
  .loc 1 2 4
  store i64, %t0, 1
  .loc 1 2 4
  br label for_head1
for_inc:
  .loc 1 1 4
  %t11 = load i64, %t1
  .loc 1 1 4
  %t12 = add %t11, 1
  .loc 1 1 4
  store i64, %t1, %t12
  .loc 1 1 4
  br label for_head
for_done:
  .loc 1 1 4
  br label exit
for_head1:
  .loc 1 2 4
  %t4 = load i64, %t0
  .loc 1 2 4
  %t5 = scmp_le %t4, 5
  .loc 1 2 4
  cbr %t5, label for_body1, label for_done1
for_body1:
  .loc 1 3 10
  %t6 = load i64, %t1
  .loc 1 3 14
  %t7 = load i64, %t0
  .loc 1 3 12
  %t8 = mul %t6, %t7
  .loc 1 3 4
  call @rt_print_i64(%t8)
  .loc 1 2 4
  br label for_inc1
for_inc1:
  .loc 1 2 4
  %t9 = load i64, %t0
  .loc 1 2 4
  %t10 = add %t9, 1
  .loc 1 2 4
  store i64, %t0, %t10
  .loc 1 2 4
  br label for_head1
for_done1:
  .loc 1 1 4
  br label for_inc
}
