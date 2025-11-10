il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
global const str @.L0 = "\n"
func @__mod_init$oop() -> void {
entry:
  ret
}
func @main() -> i64 {
entry:
  call @__mod_init$oop()
  %t0 = alloca 8
  %t1 = alloca 8
  br L10
L10:
  .loc 1 1 4
  store i64, %t1, 1
  .loc 1 1 4
  %t2 = scmp_ge 1, 0
  .loc 1 1 4
  cbr %t2, for_head_pos, for_head_neg
exit:
  ret 0
for_head_pos:
  .loc 1 1 4
  %t3 = load i64, %t1
  .loc 1 1 4
  %t4 = scmp_le %t3, 5
  .loc 1 1 4
  cbr %t4, for_body, for_done
for_head_neg:
  .loc 1 1 4
  %t5 = load i64, %t1
  .loc 1 1 4
  %t6 = scmp_ge %t5, 5
  .loc 1 1 4
  cbr %t6, for_body, for_done
for_body:
  .loc 1 2 4
  store i64, %t0, 1
  .loc 1 2 4
  %t7 = scmp_ge 1, 0
  .loc 1 2 4
  cbr %t7, for_head_pos1, for_head_neg1
for_inc:
  .loc 1 1 4
  %t18 = load i64, %t1
  .loc 1 1 4
  %t19 = iadd.ovf %t18, 1
  .loc 1 1 4
  store i64, %t1, %t19
  .loc 1 1 4
  cbr %t2, for_head_pos, for_head_neg
for_done:
  .loc 1 1 4
  br exit
for_head_pos1:
  .loc 1 2 4
  %t8 = load i64, %t0
  .loc 1 2 4
  %t9 = scmp_le %t8, 5
  .loc 1 2 4
  cbr %t9, for_body1, for_done1
for_head_neg1:
  .loc 1 2 4
  %t10 = load i64, %t0
  .loc 1 2 4
  %t11 = scmp_ge %t10, 5
  .loc 1 2 4
  cbr %t11, for_body1, for_done1
for_body1:
  .loc 1 3 10
  %t12 = load i64, %t1
  .loc 1 3 14
  %t13 = load i64, %t0
  .loc 1 3 12
  %t14 = imul.ovf %t12, %t13
  .loc 1 3 4
  call @rt_print_i64(%t14)
  .loc 1 3 4
  %t15 = const_str @.L0
  .loc 1 3 4
  call @rt_print_str(%t15)
  .loc 1 2 4
  br for_inc1
for_inc1:
  .loc 1 2 4
  %t16 = load i64, %t0
  .loc 1 2 4
  %t17 = iadd.ovf %t16, 1
  .loc 1 2 4
  store i64, %t0, %t17
  .loc 1 2 4
  cbr %t7, for_head_pos1, for_head_neg1
for_done1:
  .loc 1 1 4
  br for_inc
}
