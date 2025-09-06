il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
func @F() -> i64 {
entry_F:
  %t0 = alloca 8
  br L0_F
L0_F:
L0_F:
L0_F:
  .loc 1 2 1
  br while_head_0_F
  .loc 1 4 1
  store i64, %t0, 1
  .loc 1 4 1
  br for_head_1_F
  .loc 1 6 8
  ret 0
ret_F:
  ret 0
while_head_0_F:
  .loc 1 2 1
  %t1 = trunc1 0
  .loc 1 2 1
  cbr %t1, while_body_0_F, while_end_0_F
while_body_0_F:
  .loc 1 2 1
  br while_head_0_F
while_end_0_F:
  .loc 1 2 1
  br L0_F
for_head_1_F:
  .loc 1 4 1
  %t2 = load i64, %t0
  .loc 1 4 1
  %t3 = scmp_le %t2, 1
  .loc 1 4 1
  cbr %t3, for_body_1_F, for_end_1_F
for_body_1_F:
  .loc 1 4 1
  br for_inc_1_F
for_inc_1_F:
  .loc 1 4 1
  %t4 = load i64, %t0
  .loc 1 4 1
  %t5 = add %t4, 1
  .loc 1 4 1
  store i64, %t0, %t5
  .loc 1 4 1
  br for_head_1_F
for_end_1_F:
  .loc 1 4 1
  br L0_F
}
func @main() -> i64 {
entry:
  ret 0
exit:
  ret 0
}
