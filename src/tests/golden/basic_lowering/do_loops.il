il 0.1.2
extern @rt_print_str(str) -> void
extern @rt_print_i64(i64) -> void
extern @rt_print_f64(f64) -> void
extern @rt_len(str) -> i64
extern @rt_substr(str, i64, i64) -> str
extern @rt_str_empty() -> str
func @F() -> i64 {
entry_F:
  br L-1000000000_F
L-1000000000_F:
  .loc 1 2 1
  br do_head_0_F
L-999999999_F:
L-999999998_F:
  .loc 1 6 8
  ret 0
ret_F:
  ret 0
do_head_0_F:
  .loc 1 2 1
  %t0 = trunc1 0
  .loc 1 2 1
  cbr %t0, do_body_0_F, do_end_0_F
do_body_0_F:
  .loc 1 2 1
  br do_head_0_F
do_end_0_F:
  .loc 1 2 1
  br L-999999999_F
do_head_1_F:
  .loc 1 4 1
  %t1 = trunc1 0
  .loc 1 4 1
  cbr %t1, do_end_1_F, do_body_1_F
do_body_1_F:
  .loc 1 4 1
  br do_head_1_F
do_end_1_F:
  .loc 1 4 1
  br L-999999998_F
}
func @main() -> i64 {
entry:
  ret 0
exit:
  ret 0
}
