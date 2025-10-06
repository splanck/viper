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
L-999999999_F:
  .loc 1 5 8
  ret 0
ret_F:
  ret 0
do_head_0_F:
  .loc 1 2 1
  br do_body_0_F
do_body_0_F:
  .loc 1 3 1
  br do_end_0_F
do_end_0_F:
  .loc 1 3 1
  br L-999999999_F
}
func @main() -> i64 {
entry:
  ret 0
exit:
  ret 0
}
