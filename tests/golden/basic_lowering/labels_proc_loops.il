il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
extern @rt_trap(str) -> void
func @F() -> i64 {
entry_F:
  %t0 = alloca 8
  br L-1000000000_F
L-1000000000_F:
  .loc 1 2 1
  br while_head_0_F
L-999999999_F:
  .loc 1 4 1
  store i64, %t0, 1
  .loc 1 4 1
  %t2 = scmp_ge 1, 0
  .loc 1 4 1
  cbr %t2, for_head_pos_0_F, for_head_neg_0_F
L-999999998_F:
  .loc 1 6 8
  ret 0
ret_F:
  ret 0
while_head_0_F:
  .loc 1 2 7
  %t1 = trunc1 0
  .loc 1 2 7
  cbr %t1, while_body_0_F, while_end_0_F
while_body_0_F:
  .loc 1 2 1
  br while_head_0_F
while_end_0_F:
  .loc 1 2 1
  br L-999999999_F
for_head_pos_0_F:
  .loc 1 4 1
  %t3 = load i64, %t0
  .loc 1 4 1
  %t4 = scmp_le %t3, 1
  .loc 1 4 1
  cbr %t4, for_body_1_F, for_end_1_F
for_head_neg_0_F:
  .loc 1 4 1
  %t5 = load i64, %t0
  .loc 1 4 1
  %t6 = scmp_ge %t5, 1
  .loc 1 4 1
  cbr %t6, for_body_1_F, for_end_1_F
for_body_1_F:
  .loc 1 4 1
  br for_inc_1_F
for_inc_1_F:
  .loc 1 4 1
  %t7 = load i64, %t0
  .loc 1 4 1
  %t8 = iadd.ovf %t7, 1
  .loc 1 4 1
  store i64, %t0, %t8
  .loc 1 4 1
  cbr %t2, for_head_pos_0_F, for_head_neg_0_F
for_end_1_F:
  .loc 1 4 1
  br L-999999998_F
}
func @main() -> i64 {
entry:
  ret 0
exit:
  ret 0
}
